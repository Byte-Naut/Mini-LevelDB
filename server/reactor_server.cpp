//
// Created by ljyay on 2026/4/25.
//

#include <iostream>
#include <vector>
#include <unordered_map>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include "./server/includes/wire_protocol.h"
#include "./db/includes/mini_kv_store.h"

using namespace net;

void set_non_blocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

struct Session
{
    int fd;
    SessionState state{SessionState::READ_HEADER};

    std::vector<uint8_t> read_buf;
    size_t expected_bytes{sizeof(MsgHeader)};

    std::vector<uint8_t> write_buf;
    size_t written_bytes{0};

    MsgHeader header;
};

class ReactorServer
{
    int epfd_;
    int listen_fd_;
    std::unordered_map<int, Session> sessions_;
    mini_kv_store db_;

public:
    ReactorServer(int port)
    {
        epfd_ = epoll_create(0);
        listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
        set_non_blocking(listen_fd_);

        int opt = 1;
        setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        sockaddr_in addr{AF_INET, htons(port), {INADDR_ANY}};
        bind(listen_fd_, (struct sockaddr*)&addr, sizeof(addr));
        listen(listen_fd_, 1024);

        epoll_event ev;
        ev.events = EPOLLIN;
        ev.data.fd = listen_fd_;
        epoll_ctl(epfd_, EPOLL_CTL_ADD, listen_fd_, &ev);

        std::cout << "[SYS] Reactor 启动 端口" << port << std::endl;
    }

    void run()
    {
        epoll_event events[1024];
        while (true)
        {
            int n = epoll_wait(epfd_, events, 1024, -1);
            for (int i = 0; i < n; ++i)
            {
                int fd = events[i].data.fd;
                uint32_t ev = events[i].events;

                if (fd == listen_fd_)
                {
                    handle_accept();
                }
                else
                {
                    if (ev & EPOLLIN) handle_read(fd);
                    if (ev & EPOLLOUT) handle_write(fd);
                    if (ev & (EPOLLERR | EPOLLHUP)) close_session(fd);
                }
            }
        }
    }

private:
    void handle_accept()
    {
        while (true)
        {
            int client_fd = accept(listen_fd_, nullptr, nullptr);
            if (client_fd < 0) break;

            set_non_blocking(client_fd);
            epoll_event ev;
            ev.events = EPOLLIN;
            ev.data.fd = client_fd;
            epoll_ctl(epfd_, EPOLL_CTL_ADD, client_fd, &ev);

            sessions_[client_fd] = Session{client_fd};
        }
    }

    void handle_read(int fd)
    {
        auto& sess = sessions_[fd];
        char buf[4096];

        while (true)
        {
            ssize_t bytes = read(fd, buf, sizeof(buf));
            if (bytes < 0)
            {
                if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                close_session(fd); return;
            }
            if (bytes == 0) { close_session(fd); return;}

            sess.read_buf.insert(sess.read_buf.end(), buf, buf + bytes);
            while (sess.read_buf.size() >= sess.expected_bytes && sess.state != SessionState::WRITE_REPLY)
            {
                if (sess.state == SessionState::READ_HEADER)
                {
                    std::memcpy(&sess.header, sess.read_buf.data(), sizeof(MsgHeader));
                    if (sess.header.magic != PROTOCOL_MAGIC) {close_session(fd); return;}
                    sess.read_buf.erase(sess.read_buf.begin(), sess.read_buf.begin() + sizeof(MsgHeader));
                    sess.expected_bytes = sess.header.key_len += sess.header.value_len;
                    sess.state == SessionState::READ_BODY;
                }
                if (sess.state == SessionState::READ_BODY && sess.read_buf.size() >= sess.expected_bytes)
                {
                    std::string req_key(sess.read_buf.begin(), sess.read_buf.begin() + sess.header.key_len);
                    std::string req_val(sess.read_buf.begin() + sess.header.key_len, sess.read_buf.begin() + sess.expected_bytes);
                    sess.read_buf.erase(sess.read_buf.begin(), sess.read_buf.begin() + sess.expected_bytes);
                    process_request(sess, req_key, req_val);
                }
            }
        }
    }

    void process_request(Session& sess, const std::string& key, const std::string& val)
    {
        uint8_t status = 0x00;
        std::string reply_payload;

        if (sess.header.opcode == NetOpcode::kPut)
        {
            db_.put(key, val);
        }
        else if (sess.header.opcode == NetOpcode::kGet)
        {
            reply_payload = db_.get(key);
            if (reply_payload.empty()) status == 0x01;
        }
        else if (sess.header.opcode == NetOpcode::kDelete)
        {
            db_.erase(key);
        }
        else
        {
            status = 0xFF;
        }

        MsgHeader reply_hdr{PROTOCOL_MAGIC, sess.header.opcode, 0, static_cast<uint32_t>(1 + reply_payload.size())};

        sess.write_buf.resize(sizeof(MsgHeader) + 1 + reply_payload.size());
        std::memcpy(sess.write_buf.data(), &reply_hdr, sizeof(MsgHeader));
        sess.write_buf[sizeof(MsgHeader)] = status;

    }
};