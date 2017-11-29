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
    Connection_info() : flag_parse(false), flag_build(false), reading(true) {}
    Connection_info(const Connection_info &) = default;
    Connection_info(Connection_info &&) = default;

    Connection_info &operator=(const Connection_info &) = default;
    Connection_info &operator=(Connection_info &&) = default;

    std::string read_msg;
    std::queue<std::string> result;
    Afina::Protocol::Parser parser;
    bool flag_parse;
    bool flag_build;
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

// See Worker.h
void *Worker::OnRun(void *args) {
    std::cout << "network debug: " << __PRETTY_FUNCTION__ << std::endl;

    Worker *parent = reinterpret_cast<Worker *>(args);
    Afina::Protocol::Parser parser;
    struct epoll_event event;
    size_t maxevents = 10;
    std::vector<struct epoll_event> events(maxevents);
    ssize_t count = 1, count_write, size_buf;
    size_t parsed;
    char buf[512];
    std::string result;
    std::string temp;
    int epoll_fd;
    int s;
    int n, i;
    std::map<int, Connection_info> connections;
    int fd;
    Connection_info *conn;
	int input_fifo = parent->input_fifo;
	int output_fifo = parent->output_fifo;

    epoll_fd = epoll_create(maxevents);
    event.data.fd = parent->server_sock;
    event.events = EPOLLIN | EPOLLEXCLUSIVE;
    s = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, parent->server_sock, &event);
		if(input_fifo > 0){
			event.data.fd = input_fifo;
			event.events = EPOLLIN | EPOLLET;
			s = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, input_fifo, &event);
			if (s == -1) {
				perror("epoll_ctl");
				abort();
			}
			connections.emplace(input_fifo, Connection_info());
		}

		if(output_fifo > 0){
			event.data.fd = output_fifo;
			event.events = EPOLLOUT | EPOLLET;
			s = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, output_fifo, &event);
			if (s == -1) {
				perror("epoll_ctl");
				abort();
			}
		}
    while (parent->_running.load()) {
        n = epoll_wait(epoll_fd, events.data(), maxevents, 100);
        for (i = 0; i < n; i++) {
            if ((events[i].events & EPOLLERR) || (events[i].events & EPOLLHUP) ||
                !((events[i].events & EPOLLIN) || (events[i].events & EPOLLOUT))) {
                fprintf(stderr, "epoll error\n");
                close(events[i].data.fd);
                s = epoll_ctl(epoll_fd, EPOLL_CTL_DEL, events[i].data.fd, &event);
                continue;
            } else if (parent->server_sock == events[i].data.fd) {
                while (1) {
                    struct sockaddr in_addr;
                    socklen_t in_len;
                    int infd;
                    char hbuf[NI_MAXHOST], sbuf[NI_MAXSERV];

                    in_len = sizeof in_addr;
                    infd = accept(parent->server_sock, &in_addr, &in_len);
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
                }
            } else {
                fd = events[i].data.fd;
                conn = &(connections[fd]);
                size_buf = 0;
                if (events[i].events & EPOLLIN) {
                    while (conn->reading) {
                        try {
                            while (size_buf > 0 && !(conn->flag_parse = conn->parser.Parse(buf, size_buf, parsed))) {
                                size_buf -= parsed;
                                memcpy(buf, buf + parsed, size_buf);
                            }
                            if (!conn->flag_parse) {
                                while (((count = read(fd, buf, sizeof buf)) > 0) &&
                                       !(conn->flag_parse = conn->parser.Parse(buf, count, parsed))) {
                                }
                                size_buf = count;
                                if (count == 0) {
                                    conn->reading = false;
                                } else if ((count < 0) && (errno != EAGAIN) && (errno != EWOULDBLOCK)) {
                                    conn->reading = false;
                                }
                                if (count <= 0) {
                                    break;
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
                        if (count > 0) {
                            memcpy(buf, buf + parsed, size_buf - parsed);
                            size_buf -= parsed;
                            conn->command = conn->parser.Build(conn->size_value);
                            conn->read_msg.clear();
                            if (conn->size_value == 0) {
                            } else if (size_buf < (conn->size_value + 2)) {
                                conn->read_msg.append(buf, size_t(size_buf));
                                size_buf = 0;
                                count_write = conn->size_value + 2 - conn->read_msg.size();
                                while (count_write > 0) {
                                    if (count_write >= sizeof buf) {
                                        count_write = sizeof buf;
                                    }
                                    if ((count = read(fd, buf, count_write)) > 0) {
                                        conn->read_msg.append(buf, size_t(count));
                                    } else {
                                        if (count == 0) {
                                            conn->reading = false;
                                        } else if ((errno != EAGAIN) && (errno != EWOULDBLOCK)) {
                                            conn->reading = false;
                                        }
                                        break;
                                    }
                                    count_write = conn->size_value + 2 - conn->read_msg.size();
                                }
                            } else {
                                conn->read_msg.append(buf, conn->size_value);
                                memcpy(buf, buf + conn->size_value + 2, size_buf - size_t(conn->size_value) - 2);
                                size_buf -= conn->size_value + 2;
                            }
                            if ((conn->size_value == 0) || (conn->read_msg.size() >= (conn->size_value))) {
                                (*conn->command)
                                    .Execute(*(parent->Storage), conn->read_msg.substr(0, conn->size_value), result);
                                conn->read_msg.clear();
                                result += "\r\n";
                                conn->result.push(result);
                                result.clear();
                                conn->parser.Reset();
                                conn->flag_parse = false;
                            }
                        }
                    }
                }

                if (events[i].events & EPOLLOUT) {
                    while (!conn->result.empty()) {
                        result = conn->result.front();
                        if ((count_write = write(fd, result.data(), result.size())) < 0) {
                            throw std::runtime_error("Socket send() failed");
                        }
                        conn->result.pop();
                    }
                }

                if ((!conn->reading) && conn->result.empty()) {
                    close(events[i].data.fd);
                    connections.erase(fd);
                }
            }
        }
    }
}

} // namespace NonBlocking
} // namespace Network
} // namespace Afina
