#include <iostream>
#include <errno.h>
#include <sys/socket.h>
#include <errno.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/ip.h>

static void do_something(int connfd)
{
    char rbuf[64] = {};
    ssize_t n = read(connfd, rbuf, sizeof(rbuf) - 1);
    if(n < 0)
    {
        std::cerr << "read error\n";
        return;
    }
    std::cout << "Client says: " << rbuf << "\n";
    char wbuf[] = "World";
    write(connfd, wbuf, strlen(wbuf));
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
        do_something(connfd);
        std::cout << "Accepted connection: " << connfd << "\n";
        close(connfd);
    }
    close(fd);
    return 0;
}
