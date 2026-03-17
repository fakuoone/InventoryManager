#include "threadPool.hpp"
#include "logger.hpp"

#include <iostream>

ThreadPool::ThreadPool(std::size_t cThreadCount, Logger& cLogger) : logger_(cLogger) {
    workers_.reserve(cThreadCount);
    for (std::size_t i = 0; i < cThreadCount; ++i) {
        workers_.emplace_back(&ThreadPool::workerLoop, this);
    }
    logger_.pushLog(Log(std::format("created {} threads", cThreadCount)));
}

ThreadPool::~ThreadPool() {
    stopping_ = true;
    cv_.notify_all();

    for (auto& t : workers_) {
        if (t.joinable()) { t.join(); }
    }
    logger_.pushLog(Log(std::format("deleted threads")));
}

void ThreadPool::workerLoop() {
    while (true) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(mtx_);
            cv_.wait(lock, [this] { return stopping_ || !tasks_.empty(); });

            if (stopping_ && tasks_.empty()) { return; }

            task = std::move(tasks_.front());
            tasks_.pop();
        }
        task(); // execute outside the lock
    }
}

std::size_t ThreadPool::getAvailableThreadCount() const {
    return workers_.size() - tasks_.size();
}