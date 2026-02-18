#include <iostream>
#include <errno.h>
#include <sys/socket.h>
#include <errno.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <cassert>

static int read_all(int fd, char *buf, size_t n)
{
    while(n > 0)
    {
        ssize_t rv = read(fd, buf, n);
        if(rv <= 0)
        {
            if(errno == EINTR) continue;
            return -1;
        }
        assert((size_t)rv <= n);
        n -= (size_t)rv;
        buf += rv;
    }
    return 0;
}

static int write_all(int fd, char *buf, size_t n)
{
    while(n > 0)
    {
        ssize_t rv = write(fd, buf, n);
        if(rv <= 0) return -1;
        assert((size_t)rv <= n);
        n -= (size_t)rv;
        buf += rv;
    }
    return 0;
}

constexpr size_t k_max_msg = 4096;

static int one_request(int connfd)
{
    char rbuf[4 + k_max_msg];
    errno = 0;
    int err = read_all(connfd, rbuf, 4);
    if(err)
    {
        if(errno == 0)
            std::cerr << "ERROR: EOF\n";
        else
            std::cerr << "ERROR: could not read from socket: " 
                      << strerror(errno) 
                      << "\n";
        return err;
    }
    unsigned int len = 0;
    memcpy(&len, rbuf, sizeof(unsigned int)); // assume little endian
    if(len > k_max_msg)
    {
        std::cerr << "ERROR: message too long\n";
        return 1;
    }
    err = read_all(connfd, &rbuf[4], len);
    if(err)
    {
        std::cerr << "ERROR: could not read message\n";
        return 1;
    }
    printf("client says: %.*s\n", len, &rbuf[4]);

    const char reply[] = "world";
    char wbuf[4 + sizeof(reply)];
    len = (unsigned int)strlen(reply);
    memcpy(wbuf, &len, sizeof(unsigned int));
    memcpy(&wbuf[4], reply, len);
    return write_all(connfd, wbuf, 4 + len);
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
    while(true)
    {
        struct sockaddr_in client_addr = {};
        socklen_t addrlen = sizeof(client_addr);
        int connfd = accept(fd, (struct sockaddr *)&client_addr, &addrlen);
        if(connfd < 0)
        {
            std::cerr << "ERROR: could not accept connection: " 
                      << strerror(errno) 
                      << "\n";
            continue;
        }
        while(true)
        {
            int err = one_request(connfd);
            if(err) break;
        }
        close(connfd);
    }
    close(fd);
    return 0;
}
