#include "../../include/afina/Executor.h"
#include <iostream>

namespace Afina {
void perform(Executor *executor) {
    std::cout << "pool: " << __PRETTY_FUNCTION__ << std::endl;
    std::function<void()> task;
    std::unique_lock<std::mutex> lock(executor->mutex);
    lock.unlock();
    while (true) {
        lock.lock();
        {
            executor->empty_condition.wait(
                lock, [executor] { return !(executor->tasks.empty() && executor->state == Executor::State::kRun); });
            if (executor->state == Executor::State::kStopped ||
                (executor->state == Executor::State::kStopping && executor->tasks.empty())) {
                return;
            }
            task = executor->tasks.front();
            executor->tasks.pop_front();
        }
        lock.unlock();
        task();
    }
}

Executor::Executor(std::string name, int size) {
    std::cout << "pool: " << __PRETTY_FUNCTION__ << std::endl;
    std::cout << std::this_thread::get_id() << std::endl;
    std::lock_guard<std::mutex> lock(mutex);
    state = State ::kRun;
    for (int i = 0; i < size; ++i) {
        threads.emplace_back(std::thread(perform, this));
    }
}

void Executor::Stop(bool await) {
    std::cout << "pool: " << __PRETTY_FUNCTION__ << std::endl;
    std::unique_lock<std::mutex> lock(mutex);
    if (state == State::kRun){
        state = State::kStopping;
    }

    empty_condition.notify_all();
    if (await) {
        lock.unlock();
        for (std::thread &thread_ : threads) {
            thread_.join();
        }
        lock.lock();
        state = State::kStopped;
    }
}

Executor::~Executor() {
    std::cout << "pool: " << __PRETTY_FUNCTION__ << std::endl;
    {
        std::unique_lock<std::mutex> lock(mutex);
        if (state == State::kRun) {
            state = State::kStopping;
        }
        lock.unlock();
    }
    if (state == State::kStopping) {
        empty_condition.notify_all();
        for (std::thread &thread_ : threads) {
            thread_.join();
        }
    }
}

} // namespace Afina