#include <chrono>
#include <iostream>
#include <memory>
#include <uv.h>

#include <cxxopts.hpp>

#include <fstream>
#include <unistd.h>

#include <sys/epoll.h>
#include <sys/signalfd.h>

#include <afina/Storage.h>
#include <afina/Version.h>
#include <afina/network/Server.h>

#include "network/blocking/ServerImpl.h"
#include "network/nonblocking/ServerImpl.h"
#include "network/uv/ServerImpl.h"
#include "storage/MapBasedGlobalLockImpl.h"

typedef struct {
    std::shared_ptr<Afina::Storage> storage;
    std::shared_ptr<Afina::Network::Server> server;
} Application;

// Handle all signals catched
void signal_handler(uv_signal_t *handle, int signum) {
    Application *pApp = static_cast<Application *>(handle->data);

    std::cout << "Receive stop signal" << std::endl;
    uv_stop(handle->loop);
}

// Called when it is time to collect passive metrics from services
void timer_handler(uv_timer_t *handle) {
    Application *pApp = static_cast<Application *>(handle->data);
    std::cout << "Start passive metrics collection" << std::endl;
}

void run_event_loop(int efd) {
    int maxevents = 10;
    std::vector<struct epoll_event> events(maxevents);
    bool run = true;
    int n;
    struct signalfd_siginfo sig_info;
    while (run) {
        n = epoll_wait(efd, events.data(), maxevents, 100);
        if (n == -1) {
            throw std::runtime_error("epoll_wait() error");
        }
        for (int i = 0; i < n; ++i) {
            ssize_t read_count = read(events[i].data.fd, &sig_info, sizeof(sig_info));
            if (read_count == sizeof(sig_info) &&
                (sig_info.ssi_signo == SIGINT || sig_info.ssi_signo == SIGTERM || sig_info.ssi_signo == SIGKILL)) {
                close(events[i].data.fd);
                run = false;
                break;
            } else {
                throw std::runtime_error("Non correct signal");
            }
        }
    }
}

int main(int argc, char **argv) {
    // Build version
    // TODO: move into Version.h as a function
    std::stringstream app_string;
    app_string << "Afina " << Afina::Version_Major << "." << Afina::Version_Minor << "." << Afina::Version_Patch;
    if (Afina::Version_SHA.size() > 0) {
        app_string << "-" << Afina::Version_SHA;
    }

    // Command line arguments parsing
    cxxopts::Options options("afina", "Simple memory caching server");
    try {
        // TODO: use custom cxxopts::value to print options possible values in help message
        // and simplify validation below
        options.add_options()("s,storage", "Type of storage service to use", cxxopts::value<std::string>());
        options.add_options()("n,network", "Type of network service to use", cxxopts::value<std::string>());
        options.add_options()("h,help", "Print usage info");
        options.add_options()("p,pid", "Output pid", cxxopts::value<std::string>());
        options.add_options()("d,daemon", "Demon");
        options.add_options()("r", "input FIFO", cxxopts::value<std::string>());
        options.add_options()("w", "output FIFO", cxxopts::value<std::string>());
        options.parse(argc, argv);

        if (options.count("help") > 0) {
            std::cerr << options.help() << std::endl;
            return 0;
        }
    } catch (cxxopts::OptionParseException &ex) {
        std::cerr << "Error: " << ex.what() << std::endl;
        return 1;
    }

    if (options.count("daemon") > 0) {
        pid_t pid;
        pid = fork();
        if (!pid) {

            setsid();
            close(STDIN_FILENO);
            close(STDOUT_FILENO);
            close(STDERR_FILENO);

        } else {
            return 0;
        }
    }
    if (options.count("pid") > 0) {
        std::string file_out_pid;
        file_out_pid = options["pid"].as<std::string>();
        std::ofstream pid_out;
        pid_out.open(file_out_pid);
        pid_out << getpid();
        pid_out.close();
    }
    // Start boot sequence
    Application app;
    std::cout << "Starting " << app_string.str() << std::endl;
    // Build new storage instance
    std::string storage_type = "map_global";
    if (options.count("storage") > 0) {
        storage_type = options["storage"].as<std::string>();
    }

    if (storage_type == "map_global") {
        app.storage = std::make_shared<Afina::Backend::MapBasedGlobalLockImpl>();
//    } else if (storage_type == "striped") {
//        app.storage = std::make_shared<Afina::Backend::Striped_storage>(10000);
    } else {
        throw std::runtime_error("Unknown storage type");
    }

	int input_fifo = -1;
	int output_fifo = -1;
	if(options.count("r") > 0){
		std::string name_input_fifo = options["r"].as<std::string>();
		unlink(name_input_fifo.data());
		if(mkfifo(name_input_fifo.data(), S_IFIFO | S_IRUSR) < 0){
			throw std::runtime_error("FIFO make failed");
		}
		if((input_fifo = open(name_input_fifo.data(), O_NONBLOCK | O_RDONLY)) < 0){
			throw std::runtime_error("FIFO open failed");
		}
	}

	if(options.count("w") > 0){
		std::string name_output_fifo = options["w"].as<std::string>();
		unlink(name_output_fifo.data());
		if(mkfifo(name_output_fifo.data(), S_IFIFO | S_IWUSR) < 0){
			throw std::runtime_error("FIFO make failed");
		}
		if((output_fifo = open(name_output_fifo.data(), O_NONBLOCK | O_WRONLY)) < 0){
			throw std::runtime_error("FIFO open failed");
		}
	}

    // Build  & start network layer
    std::string network_type = "uv";
    if (options.count("network") > 0) {
        network_type = options["network"].as<std::string>();
    }

    if (network_type == "uv") {
        app.server = std::make_shared<Afina::Network::UV::ServerImpl>(app.storage);
    } else if (network_type == "blocking") {
        app.server = std::make_shared<Afina::Network::Blocking::ServerImpl>(app.storage);
    } else if (network_type == "nonblocking") {
        app.server = std::make_shared<Afina::Network::NonBlocking::ServerImpl>(app.storage);
    } else {
        throw std::runtime_error("Unknown network type");
    }

    // Init local loop. It will react to signals and performs some metrics collections. Each
    // subsystem is able to push metrics actively, but some metrics could be collected only
    // by polling, so loop here will does that work

    int efd;
    int signal_fd;
    epoll_event event;
    int n;
    if ((efd = epoll_create1(0)) < 0) {
        std::cout << "efd = " << efd << std::endl;
        throw std::runtime_error("epoll create error");
    }
    sigset_t mask, orig_mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGTERM);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGKILL);
    if (sigprocmask(SIG_BLOCK, &mask, &orig_mask) < 0) {
        throw std::runtime_error("sigprocmask error");
    }
    if ((signal_fd = signalfd(-1, &mask, SFD_NONBLOCK)) < 0) {
        throw std::runtime_error("signalfd error");
    }
    event.data.fd = signal_fd;
    event.events = EPOLLIN;
    if (epoll_ctl(efd, EPOLL_CTL_ADD, signal_fd, &event) < 0) {
        throw std::runtime_error("epoll_ctl() error");
    }

    // Start services
    try {
        app.storage->Start();
        app.server->Start(8080);

        // Freeze current thread and process events
        std::cout << "Application started" << std::endl;
        //        uv_run(&loop, UV_RUN_DEFAULT);
        run_event_loop(efd);
        // Stop services
        app.server->Stop();
        app.server->Join();
        app.storage->Stop();

        close(efd);
        close(signal_fd);
        std::cout << "Application stopped" << std::endl;
    } catch (std::exception &e) {
        std::cerr << "Fatal error" << e.what() << std::endl;
    }

    return 0;
}
