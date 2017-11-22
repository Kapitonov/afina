#include "Worker.h"

#include <iostream>

#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>

#include "../../protocol/Parser.h"
#include "Utils.h"
#include <afina/execute/Command.h>
#include <map>
#include <netdb.h>
#include <unistd.h>
#include <vector>

namespace Afina {
namespace Network {
namespace NonBlocking {

class Connection_info {
public:
    Connection_info() : flag_parse(false), flag_build(false) {
        //		parser = Afina::Protocol::Parser();
    }
    Connection_info(const Connection_info &) = default;            // = delete;
    Connection_info(Connection_info &&) = default;                 // = delete;
    Connection_info &operator=(const Connection_info &) = default; // = delete;
    Connection_info &operator=(Connection_info &&) = default;
    std::string read_msg;
    std::string result;
    Afina::Protocol::Parser parser;
    bool flag_parse, flag_build;
    uint32_t size_value;
    std::unique_ptr<Execute::Command> command;
};

// See Worker.h
Worker::Worker(std::shared_ptr<Afina::Storage> ps) : _running(false), Storage(ps) {
    // TODO: implementation here
    //    Storage = ps;
    //    running = false;
}

// See Worker.h
Worker::~Worker() {
    // TODO: implementation here
}

Worker::Worker(Worker &&other)
    : _running(other._running.load()), _thread(std::move(other._thread)), Storage(std::move(other.Storage)),
      server_cock(std::move(other.server_cock)) {}

// See Worker.h
void Worker::Start(int server_socket) {
    std::cout << "network debug: " << __PRETTY_FUNCTION__ << std::endl;
    //    this->running.store(true);
    server_cock = server_socket;
    _running.store(true);
    if (pthread_create(&_thread, NULL, OnRun, this) < 0) {
        throw std::runtime_error("Could not create server thread");
    }
    // TODO: implementation here
}

// See Worker.h
void Worker::Stop() {
    std::cout << "network debug: " << __PRETTY_FUNCTION__ << std::endl;
    _running.store(false);
    // TODO: implementation here
}

// See Worker.h
void Worker::Join() {
    std::cout << "network debug: " << __PRETTY_FUNCTION__ << std::endl;
    // TODO: implementation here
    pthread_join(_thread, 0);
}

// See Worker.h
void *Worker::OnRun(void *args) {
    std::cout << "network debug: " << __PRETTY_FUNCTION__ << std::endl;

    // TODO: implementation here
    // 1. Create epoll_context here
    // 2. Add server_socket to context
    // 3. Accept new connections, don't forget to call make_socket_nonblocking on
    //    the client socket descriptor
    // 4. Add connections to the local context
    // 5. Process connection events
    //
    // Do not forget to use EPOLLEXCLUSIVE flag when register socket
    // for events to avoid thundering herd type behavior.

    Worker *parent = reinterpret_cast<Worker *>(args);
    Afina::Protocol::Parser parser;
    struct epoll_event event;
    int maxevents = 10;
    std::vector<struct epoll_event> events(maxevents);
    ssize_t count;
    size_t parsed;
    char buf[512];
    std::string result;
    int epoll_fd;
    int s;
    int n, i;
    std::map<int, Connection_info> connections;
    int fd;

    epoll_fd = epoll_create(maxevents);
    event.data.fd = parent->server_cock;
    event.events = EPOLLIN | EPOLLEXCLUSIVE;
    s = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, parent->server_cock, &event);
    while (parent->_running.load()) {
        n = epoll_wait(epoll_fd, events.data(), maxevents, 100);
        if (n > 0) {
            std::cout << n << std::endl;
        }
        for (i = 0; i < n; i++) {
            if ((events[i].events & EPOLLERR) || (events[i].events & EPOLLHUP) || (!(events[i].events & EPOLLIN))) {
                /* An error has occured on this fd, or the socket is not
                   ready for reading (why were we notified then?) */
                fprintf(stderr, "epoll error\n");
                close(events[i].data.fd);
                s = epoll_ctl(epoll_fd, EPOLL_CTL_DEL, events[i].data.fd, &event);
                continue;
            } else if (parent->server_cock == events[i].data.fd) {
                while (1) {
                    struct sockaddr in_addr;
                    socklen_t in_len;
                    int infd;
                    char hbuf[NI_MAXHOST], sbuf[NI_MAXSERV];

                    in_len = sizeof in_addr;
                    infd = accept(parent->server_cock, &in_addr, &in_len);
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
                        std::cout << "pid = " << getpid() << std::endl;
                    }

                    make_socket_non_blocking(infd);

                    event.data.fd = infd;
                    event.events = EPOLLIN | EPOLLET;
                    s = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, infd, &event);
                    if (s == -1) {
                        perror("epoll_ctl");
                        abort();
                    }
                    connections.emplace(infd, Connection_info());
                }
            } else {
                fd = events[i].data.fd;
                bool done = true;
                Connection_info *conn = &connections[fd];
                while (true) {
                    done = false;
                    if ((count = read(events[i].data.fd, buf, sizeof buf)) > 0) {
                        conn->read_msg.append(buf, count);
                    }
                    if ((count <= 0) && conn->read_msg.empty() && (!conn->flag_parse) && (!conn->flag_build)) {
                        if (count == 0) {
                            done = true;
                        }
                        break;
                    }
                    {
                        if (!conn->flag_parse) {
                            //                            std::cout << "PARSE\n";
                            try {
                                conn->flag_parse = parser.Parse(conn->read_msg, parsed);
                            } catch (const std::runtime_error &error) {
                                done = true;
                                //                                std::cout << "ERROR\n" << error.what() << std::endl;
                                conn->result += "ERROR\r\n";
                                conn->read_msg.clear();
                                parser.Reset();
                                if (send(events[i].data.fd, conn->result.data(), conn->result.size(), 0) <= 0) {
                                    throw std::runtime_error("Socket send() failed");
                                }
                                conn->result.clear();
                                break;
                            }

                            conn->read_msg.erase(0, parsed);
                            parsed = 0;
                        }
                        if (conn->flag_parse && !conn->flag_build) {
                            //                            std::cout << "BUILD\n";
                            conn->command = parser.Build(conn->size_value);
                            conn->flag_build = true;
                        }
                        if ((conn->read_msg.size() >= conn->size_value + 2 || conn->size_value == 0) &&
                            conn->flag_build) {
                            //                            std::cout << "EXECUTE\n";
                            conn->result.clear();
                            (*conn->command)
                                .Execute(*(parent->Storage), conn->read_msg.substr(0, conn->size_value), conn->result);
                            conn->result += "\r\n";
                            //                            std::cout << "result: " << conn->result << std::endl;
                            if (write(events[i].data.fd, conn->result.data(), conn->result.size()) <= 0) {
                                throw std::runtime_error("Socket send() failed");
                            }
                            if (conn->size_value > 0) {
                                conn->read_msg.erase(0, conn->size_value + 2);
                            }
                            conn->flag_parse = false;
                            conn->flag_build = false;
                            parser.Reset();
                        }
                    }
                }
                if (done) {
                    std::cout << "close\n";
                    close(events[i].data.fd);
                }
            }
        }
    }
}

} // namespace NonBlocking
} // namespace Network
} // namespace Afina
