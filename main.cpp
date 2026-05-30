#include <iostream>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>

void set_nonblocking(int fd)
{
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
}

int main()
{
    int lsn_fd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{AF_INET, htons(8080), {INADDR_ANY}};
    bind(lsn_fd, (sockaddr*)&addr, sizeof(addr));
    listen(lsn_fd, 5);

    int ep_fd = epoll_create1(0);
    epoll_event ev, events[64];
    ev.events = EPOLLIN;
    ev.data.fd = lsn_fd;
    epoll_ctl(ep_fd, EPOLL_CTL_ADD, lsn_fd, &ev);

    std::cout << "[SYS] epoll启动，监听8080" << std::endl;

    while (true)
    {
        int n = epoll_wait(ep_fd, events, 64, -1);

        for (int i = 0; i < n; ++i)
        {
            int fd = events[i].data.fd;

            if (fd == lsn_fd)
            {
                int client_fd = accept(lsn_fd, nullptr, nullptr);
                set_nonblocking(client_fd);
                ev.events = EPOLLIN;
                ev.data.fd = client_fd;
                epoll_ctl(ep_fd, EPOLL_CTL_ADD, client_fd, &ev);
                std::cout << "[SYS] 捕获新信箱：" << client_fd << std::endl;
            }
            else
            {
                char buf[1024];
                ssize_t bytes = read(fd, buf, sizeof(buf));

                if (bytes > 0)
                {
                    buf[bytes] = '\0';
                    std::cout << "[RECV]" << buf;
                    write(fd, buf, bytes);
                }
                else
                {
                    epoll_ctl(ep_fd, EPOLL_CTL_DEL, fd, nullptr);
                    close(fd);
                }
            }
        }
    }
    return 0;
}