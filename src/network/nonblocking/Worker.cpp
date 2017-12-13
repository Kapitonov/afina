#include "Worker.h"

#include <iostream>

#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>

#include "Utils.h"
#include <afina/execute/Command.h>
#include <cstring>
#include <map>
#include <netdb.h>
#include <protocol/Parser.h>
#include <queue>
#include <unistd.h>

namespace Afina {
namespace Network {
namespace NonBlocking {

class Connection_info {
public:
    Connection_info() : flag_parse(false), reading(true) {}
    Connection_info(const Connection_info &) = default;
    Connection_info(Connection_info &&) = default;

    Connection_info &operator=(const Connection_info &) = default;
    Connection_info &operator=(Connection_info &&) = default;

    std::string value;
    std::queue<std::string> result;
    Afina::Protocol::Parser parser;
    std::string buf;
    bool flag_parse;
    bool reading;
    uint32_t size_value;
    std::unique_ptr<Execute::Command> command;
};

// See Worker.h
Worker::Worker(std::shared_ptr<Afina::Storage> ps) : _running(false), Storage(ps) {}

// See Worker.h
Worker::~Worker() {}

Worker::Worker(Worker &&other)
    : _running(other._running.load()), _thread(std::move(other._thread)), Storage(std::move(other.Storage)),
      server_sock(std::move(other.server_sock)) {}

// See Worker.h
void Worker::Start(int server_socket) {
    std::cout << "network debug: " << __PRETTY_FUNCTION__ << std::endl;
    server_sock = server_socket;
    _running.store(true);
    input_fifo = -1;
    output_fifo = -1;
    if (pthread_create(&_thread, NULL, OnRun, this) < 0) {
        throw std::runtime_error("Could not create server thread");
    }
}

void Worker::Start(int server_socket, int input, int output) {
    std::cout << "network debug: " << __PRETTY_FUNCTION__ << std::endl;
    //    this->running.store(true);
    server_sock = server_socket;
    _running.store(true);
    input_fifo = input;
    output_fifo = output;
    if (pthread_create(&_thread, NULL, OnRun, this) < 0) {
        throw std::runtime_error("Could not create server thread");
    }
}

// See Worker.h
void Worker::Stop() {
    std::cout << "network debug: " << __PRETTY_FUNCTION__ << std::endl;
    _running.store(false);
}

// See Worker.h
void Worker::Join() {
    std::cout << "network debug: " << __PRETTY_FUNCTION__ << std::endl;
    pthread_join(_thread, 0);
}

void *Worker::OnRun(void *args) {
    Worker *parent = reinterpret_cast<Worker *>(args);
    parent->Run();
}

// See Worker.h
void Worker::Run() {
    std::cout << "network debug: " << __PRETTY_FUNCTION__ << std::endl;

    //    Worker *parent = reinterpret_cast<Worker *>(args);
    Afina::Protocol::Parser parser;
    struct epoll_event event;
    size_t maxevents = 10;
    std::vector<struct epoll_event> events(maxevents);
    ssize_t count = 1, count_write, size_buf;
    size_t parsed;
    char buf[4096];
    std::string result;
    std::string temp;
    int epoll_fd;
    int s;
    int n, i;
    std::map<int, Connection_info> connections;
    int fd;
    Connection_info *conn;

    epoll_fd = epoll_create(maxevents);
    event.data.fd = server_sock;
    event.events = EPOLLIN | EPOLLEXCLUSIVE;
    s = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_sock, &event);
    if (input_fifo > 0) {
        event.data.fd = input_fifo;
        event.events = EPOLLIN | EPOLLET;
        s = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, input_fifo, &event);
        if (s == -1) {
            perror("epoll_ctl");
            abort();
        }
        connections.emplace(input_fifo, Connection_info());
    }

    if (output_fifo > 0) {
        event.data.fd = output_fifo;
        event.events = EPOLLOUT | EPOLLET;
        s = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, output_fifo, &event);
        if (s == -1) {
            perror("epoll_ctl");
            abort();
        }
    }
    while (_running.load()) {
        n = epoll_wait(epoll_fd, events.data(), maxevents, 100);
        for (i = 0; i < n; i++) {
            if ((events[i].events & EPOLLERR) || (events[i].events & EPOLLHUP) ||
                !((events[i].events & EPOLLIN) || (events[i].events & EPOLLOUT))) {
                fprintf(stderr, "epoll error\n");
                close(events[i].data.fd);
                s = epoll_ctl(epoll_fd, EPOLL_CTL_DEL, events[i].data.fd, &event);
                continue;
            } else if (server_sock == events[i].data.fd) {
                struct sockaddr in_addr;
                socklen_t in_len;
                int infd;
                char hbuf[NI_MAXHOST], sbuf[NI_MAXSERV];

                in_len = sizeof in_addr;
                infd = accept(server_sock, &in_addr, &in_len);
                if (infd == -1) {
                    if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
                        break;
                    } else {
                        perror("accept");
                        break;
                    }
                }

                s = getnameinfo(&in_addr, in_len, hbuf, sizeof hbuf, sbuf, sizeof sbuf,
                                NI_NUMERICHOST | NI_NUMERICSERV);
                if (s == 0) {
                    printf("Accepted connection on descriptor %d "
                           "(host=%s, port=%s)\n",
                           infd, hbuf, sbuf);
                }

                make_socket_non_blocking(infd);

                event.data.fd = infd;
                event.events = EPOLLIN | EPOLLOUT | EPOLLET;
                s = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, infd, &event);
                if (s == -1) {
                    perror("epoll_ctl");
                    abort();
                }
                connections.emplace(infd, Connection_info());
            } else {
                fd = events[i].data.fd;
                conn = &(connections[fd]);
                size_buf = conn->buf.size();
                memcpy(buf, conn->buf.data(), (size_t)size_buf);
                if (events[i].events & EPOLLIN) {
                    do {
                        std::cout << "size_buf: " << size_buf << "; parsed: " << parsed
                                  << "; reading: " << conn->reading << std::endl;
                        try {
                            if (!conn->flag_parse) {
                                while (!conn->flag_parse && size_buf > 0 && parsed > 0) {
                                    conn->flag_parse = conn->parser.Parse(buf, size_buf, parsed);
                                    size_buf -= parsed;
                                    memcpy(buf, buf + parsed, size_buf);
                                }
                            }
                            if (conn->reading) {
                                while (((count = read(fd, buf + size_buf, (sizeof buf) - size_buf)) > 0) &&
                                       !conn->flag_parse) {
                                    conn->flag_parse = conn->parser.Parse(buf, size_buf + (size_t)count, parsed);
                                    size_buf += count - parsed;
                                    memcpy(buf, buf + parsed, (size_t)size_buf);
                                }

                                if (count == 0) {
                                    conn->reading = false;
                                } else if (count < 0 && ((errno != EAGAIN) && (errno != EWOULDBLOCK))) {
                                    conn->reading = false;
                                }
                            }
                        } catch (const std::runtime_error &error) {
                            result = "ERROR\r\n";
                            conn->result.push(result);
                            result.clear();
                            conn->parser.Reset();
                            conn->reading = false;
                            break;
                        }
                        if (conn->flag_parse) {
                            conn->command = conn->parser.Build(conn->size_value);
                            conn->value.clear();
                            if (conn->size_value == 0) {
                            } else if (size_buf < (conn->size_value + 2)) {
                                conn->value.append(buf, size_t(size_buf));
                                size_buf = 0;
                                count_write = conn->size_value + 2 - conn->value.size();
                                while (count_write > 0) {
                                    if (count_write >= sizeof buf) {
                                        count_write = sizeof buf;
                                    }
                                    if ((count = read(fd, buf, count_write)) > 0) {
                                        conn->value.append(buf, size_t(count));
                                    } else {
                                        if (count == 0) {
                                            conn->reading = false;
                                        } else if ((errno != EAGAIN) && (errno != EWOULDBLOCK)) {
                                            conn->reading = false;
                                        }
                                        break;
                                    }
                                    count_write = conn->size_value + 2 - conn->value.size();
                                }
                            } else {
                                conn->value.append(buf, conn->size_value);
                                memcpy(buf, buf + conn->size_value + 2, size_buf - size_t(conn->size_value) - 2);
                                size_buf -= conn->size_value + 2;
                            }
                            if ((conn->size_value == 0) || (conn->value.size() >= (conn->size_value))) {
                                (*conn->command).Execute(*(Storage), conn->value.substr(0, conn->size_value), result);
                                conn->value.clear();
                                result += "\r\n";
                                conn->result.push(result);
                                result.clear();
                                conn->parser.Reset();
                                conn->flag_parse = false;
                            }
                        }
                    } while ((size_buf > 0 && parsed > 0));
                }
                if (events[i].events & EPOLLOUT) {
                    while (!conn->result.empty()) {
                        result = conn->result.front();
                        if ((count_write = write(fd, result.data(), result.size())) < 0) {
                            if ((errno != EAGAIN) && (errno != EWOULDBLOCK)) {
                                throw std::runtime_error("Socket send() failed");
                            }
                        }
                        if(count_write < result.size()){
                            conn->result.front().erase(count_write);
                        }else{
                            conn->result.pop();
                        }
                    }
                }
                if ((!conn->reading) && conn->result.empty()) {
                    close(events[i].data.fd);
                    connections.erase(fd);
                }

                conn->buf.clear();
                conn->buf.append(buf, size_buf);
            }
        }
    }
} // namespace NonBlocking

} // namespace NonBlocking
} // namespace Network
} // namespace Afina
