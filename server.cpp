#include <iostream>
#include <errno.h>
#include <sys/socket.h>
#include <errno.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <cassert>
#include <poll.h>
#include <vector>
#include <fcntl.h>
#include <map>

struct Conn
{
    int fd = -1;
    bool want_read  = false;
    bool want_write = false;
    bool want_close = false;
    std::vector<uint8_t> incoming;
    std::vector<uint8_t> outgoing;
};

static void fd_set_nb(int fd)
{
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
}

constexpr size_t k_max_msg = 32 << 20;

static void buf_append(std::vector<uint8_t> &buf, const uint8_t *data, size_t len)
{
    buf.insert(buf.end(), data, data+len);
}

static void buf_consume(std::vector<uint8_t> &buf, size_t n)
{
    buf.erase(buf.begin(), buf.begin()+n);
}

static bool read_u32(const uint8_t *&cur, const uint8_t *end, uint32_t &out)
{
    if (cur + 4 > end) return false;
    memcpy(&out, cur, 4);
    cur += 4;
    return true;
}

static bool
read_str(const uint8_t *&cur, const uint8_t *end, size_t n, std::string &out)
{
    if (cur + n > end) return false;
    out.assign(cur, cur + n);
    cur += n;
    return true;
}
enum 
{
    RES_OK = 0,
    RES_ERR = 1,
    RES_NX = 2,
};

struct Response
{
    unsigned int status = 0;
    std::vector<uint8_t> data;
};

static std::map<std::string, std::string> g_data;

static void
do_request(std::vector<std::string> &cmd, Response &out)
{
    if(cmd.size() == 2 && cmd[0] == "get")
    {
        auto it = g_data.find(cmd[1]);
        if(it == g_data.end())
        {
            out.status = RES_NX;
            return;
        }
        const std::string &val = it->second;
        out.data.assign(val.begin(), val.end());
    }
    else if(cmd.size() == 3 && cmd[0] == "set")
    {
        g_data[cmd[1]].swap(cmd[2]);
    }
    else if(cmd.size() == 2 && cmd[0] == "del")
    {
        g_data.erase(cmd[1]);
    }
    else
    {
        out.status = RES_ERR;
    }
}

static void 
make_response(const Response &resp, std::vector<uint8_t> &out)
{
    uint32_t resp_len = 4 + (uint32_t)resp.data.size();
    buf_append(out, (const uint8_t *)&resp_len, 4);
    buf_append(out, (const uint8_t *)&resp.status, 4);
    buf_append(out, resp.data.data(), resp.data.size());
}

static int 
parse_req(const uint8_t *data, size_t size, std::vector<std::string> &out)
{
    const uint8_t *end = data + size;
    unsigned int nstr = 0;
    if(!read_u32(data, end, nstr)) return -1;
    if(nstr > k_max_msg) return -1;
    while(out.size() < nstr)
    {
        unsigned len = 0;
        if(!read_u32(data, end, len)) return -1;
        out.push_back(std::string());
        if(!read_str(data, end, len, out.back())) return -1;
    }
    if(data != end) return -1;
    return 0;
}

static bool try_one_request(Conn *conn)
{
    if(conn->incoming.size() < 4)
    {
        return false;
    }
    int len = 0;
    memcpy(&len, conn->incoming.data(), 4);
    if(len > (int)k_max_msg)
    {
        conn->want_close = true;
        return false;
    }
    if(4 + len > (int)conn->incoming.size()) return false;
    const uint8_t *request = &conn->incoming[4];
    buf_consume(conn->incoming, 4 + len);
    std::vector<std::string> cmd;
    if(parse_req(request, len, cmd) < 0)
    {
        conn->want_close = true;
        return false;
    }
    Response resp;
    do_request(cmd, resp);
    make_response(resp, conn->outgoing);
    return true;
}

static void handle_write(Conn *conn)
{
    assert(conn->outgoing.size() > 0);
    ssize_t rv = write(conn->fd, conn->outgoing.data(), conn->outgoing.size());
    if(rv < 0)
    {
        if(errno == EAGAIN) return;
        conn->want_close = true;
        return;
    }
    buf_consume(conn->outgoing, (size_t)rv);
    if(conn->outgoing.size() == 0)
    {
        conn->want_read = true;
        conn->want_write = false;
    }
}


static void handle_read(Conn *conn)
{
    uint8_t buf[64 * 1024];
    ssize_t rv = read(conn->fd, buf, sizeof(buf));
    if(rv <= 0)
    {
        conn->want_close = true;
        return;
    }
    buf_append(conn->incoming, buf, (size_t)rv);
    while(try_one_request(conn));
    if(conn->outgoing.size() > 0)
    {
        conn->want_read = false;
        conn->want_write = true;
        return handle_write(conn);
    }
}

static Conn *handle_accept(int fd)
{
    struct sockaddr_in client_addr = {};
    socklen_t addrlen = sizeof(client_addr);
    int connfd = accept(fd, (struct sockaddr*)&client_addr, &addrlen);
    if(connfd < 0) return NULL;
    fd_set_nb(connfd);
    Conn *conn = new Conn();
    conn->fd = connfd;
    conn->want_read = true;
    return conn;
}

int main(void)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if(fd < 0)
    {
        std::cerr << "ERROR: could not create socket: " 
                  << strerror(errno) 
                  << "\n";
        return 1;
    }
    int val = 1;
    if(setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val)) < 0)
    {
        std::cerr << "ERROR: could not set socket options: " 
                  << strerror(errno) 
                  << "\n";
        close(fd);
        return 1;
    }
    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = ntohs(1234);
    addr.sin_addr.s_addr = ntohl(0); 
    int rv = bind(fd, (const struct sockaddr *)&addr, sizeof(addr));
    if(rv < 0)
    {
        std::cerr << "ERROR: could not bind socket: " 
                  << strerror(errno) 
                  << "\n";
        close(fd);
        return 1;
    }
    rv = listen(fd, SOMAXCONN);
    if(rv < 0)
    {
        std::cerr << "ERROR: could not listen to socket: " 
                  << strerror(errno) 
                  << "\n";
        close(fd);
        return 1;
    }
    std::cout << "Listening on 0.0.0.0:1234\n";

    std::vector<Conn*> fd2conn;
    std::vector<struct pollfd> poll_args;
    fd_set_nb(fd);
    while(true)
    {
        poll_args.clear();
        struct pollfd pfd = {fd, POLLIN, 0};
        poll_args.push_back(pfd);
        for(Conn *conn: fd2conn)
        {
            if(!conn)
            {
                continue;
            }
            struct pollfd pfd = {conn->fd, POLLERR, 0};
            if(conn->want_read)
            {
                pfd.events |= POLLIN;
            }
            if(conn->want_write)
            {
                pfd.events |= POLLOUT;
            }
            poll_args.push_back(pfd);
        }
        int rv = poll(poll_args.data(), (nfds_t)poll_args.size(), -1);
        if(rv < 0 && errno == EINTR) continue;
        if(rv < 0)
        {
            std::cerr << "ERROR: could not poll events"
                      << strerror(errno)
                      << "\n";
            return 1;
        }
        if(poll_args[0].revents)
        {
            if(Conn *conn = handle_accept(fd))
            {
                if(fd2conn.size() <= (size_t)conn->fd)
                    fd2conn.resize(conn->fd + 1);
                fd2conn[conn->fd] = conn;
            }
        }
        for(size_t i = 1; i < poll_args.size(); ++i)
        {
            int ready = poll_args[i].revents;
            Conn *conn = fd2conn[poll_args[i].fd];
            if(ready & POLLIN)  handle_read(conn);
            if(ready & POLLOUT) handle_write(conn);
            if(ready & POLLERR || conn->want_close)
            {
                (void)close(conn->fd);
                fd2conn[conn->fd] = NULL;
                delete conn;
            }
        }
    }
    return 0;
}
