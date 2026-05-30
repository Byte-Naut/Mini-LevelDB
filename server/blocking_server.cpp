//
// Created by ljyay on 2026/4/25.
//

#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

int main()
{
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{AF_INET, htons(8080), {INADDR_ANY}};
    bind(server_fd, (struct sockaddr*)&addr, sizeof(addr));
    listen(server_fd, 5);

    std::cout << "[SYS] 阻塞Sever启动，监听8080" << std::endl;

    while (true)
    {
        std::cout << "[SYS] 主线程挂起，等待连接" << std::endl;
        int client_fd = accept(server_fd, nullptr, nullptr);
        std::cout << "[SYS] 捕获客户端连接，主进程阻塞" << std::endl;

        char buf[1024];
        while (true)
        {
            // 客户端不发包，主线程在此阻塞，不可接入
            ssize_t bytes = read(client_fd, buf, sizeof(buf)); //?
            if (bytes <= 0) break; // 断开

            buf[bytes] = '\0';
            std::cout << "[RECV]" << buf;
            write(client_fd, buf, bytes); // 回写
        }
        close(client_fd);
        std::cout << "[SYS] 客户端断开，主线程返回accept重新挂起" << std::endl;
    }
    return 0;
}