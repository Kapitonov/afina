//#include "afina/Executor.h"
#include "../../include/afina/Executor.h"


namespace Afina {
    void perform(Executor *executor) {
        std::function<void()> task;
        while(true){
            {
                std::unique_lock<std::mutex> lock(executor->mutex);
                executor->empty_condition.wait(lock, [executor] { return (!(executor->tasks.empty()) ||
                        executor->state != Executor::State::kRun); });
                if (executor->state == Executor::State::kStopped || executor->tasks.size() == 0) {
                    return;
                }
                task = std::move(executor->tasks.front());
                executor->tasks.pop_front();
            }
            task();
        }
    }

    Executor::Executor(std::string name, int size) {
        std::lock_guard<std::mutex> lock(mutex);
        for(int i = 0; i < size; ++i){
            threads.emplace_back(perform, this);
        }
        state = State ::kRun;
    }

    void Executor::Stop(bool await){
        {
            std::lock_guard<std::mutex> lock(mutex);
            state = State::kStopping;
        }
        if(await){
            empty_condition.notify_all();
            for(std::thread &thread_ : threads){
                thread_.join();
            }
        }
    }

    Executor::~Executor() {
        {
            std::lock_guard<std::mutex> lock(mutex);
            if(state == State::kRun) {
                state = State::kStopped;
            }
        }
        if(state == State::kStopping){
            empty_condition.notify_all();
            for(std::thread &thread_ : threads){
                thread_.join();
            }
        }
    }

}