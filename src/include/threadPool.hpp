#pragma once

#include "logger.hpp"

#include <atomic>
#include <condition_variable>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <thread>
#include <type_traits>
#include <vector>

class ThreadPool {
   public:
    explicit ThreadPool(std::size_t cThreadCount, Logger& cLogger);
    ~ThreadPool();

    // Submit any callable (including member functions)
    template <typename F, typename... Args>
    auto submit(F&& f, Args&&... args) {
        using R = std::invoke_result_t<std::decay_t<F>, std::decay_t<Args>...>;
        auto task = std::make_shared<std::packaged_task<R()>>([f = std::forward<F>(f), ... args = std::forward<Args>(args)]() mutable { return std::invoke(f, std::move(args)...); });

        std::future<R> fut = task->get_future();

        {
            std::lock_guard<std::mutex> lock(mtx);
            tasks.emplace([task] { (*task)(); });
        }
        cv.notify_one();
        return fut;
    }

   private:
    void workerLoop();

    Logger& logger;
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;

    std::mutex mtx;
    std::condition_variable cv;
    std::atomic<bool> stopping{false};
};
