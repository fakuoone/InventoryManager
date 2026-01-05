#include "threadPool.hpp"
#include "logger.hpp"

#include <iostream>

ThreadPool::ThreadPool(std::size_t cThreadCount, Logger& cLogger) : logger(cLogger) {
    workers.reserve(cThreadCount);
    for (std::size_t i = 0; i < cThreadCount; ++i) {
        workers.emplace_back(&ThreadPool::workerLoop, this);
    }
    logger.pushLog(Log(std::format("created {} threads", cThreadCount)));
}

ThreadPool::~ThreadPool() {
    stopping = true;
    cv.notify_all();

    for (auto& t : workers) {
        if (t.joinable()) { t.join(); }
    }
    logger.pushLog(Log(std::format("deleted threads")));
}

void ThreadPool::workerLoop() {
    while (true) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(mtx);
            cv.wait(lock, [this] { return stopping || !tasks.empty(); });

            if (stopping && tasks.empty()) { return; }

            task = std::move(tasks.front());
            tasks.pop();
        }
        task();  // execute outside the lock
    }
}