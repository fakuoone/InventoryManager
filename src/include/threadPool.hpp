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
    ThreadPool(std::size_t cThreadCount, Logger& cLogger);
    ~ThreadPool();

    // Submit any callable (including member functions)
    template <typename F, typename... Args> auto submit(F&& f, Args&&... args) {
        using R = std::invoke_result_t<std::decay_t<F>, std::decay_t<Args>...>;
        auto task = std::make_shared<std::packaged_task<R()>>(
            [f = std::forward<F>(f), ... args = std::forward<Args>(args)]() mutable { return std::invoke(f, std::move(args)...); });

        std::future<R> fut = task->get_future();

        {
            std::lock_guard<std::mutex> lock(mtx_);
            tasks_.emplace([task] { (*task)(); });
        }
        cv_.notify_one();
        return fut;
    }

    std::size_t getAvailableThreadCount() const;

  private:
    void workerLoop();

    Logger& logger_;
    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;

    std::mutex mtx_;
    std::condition_variable cv_;
    std::atomic<bool> stopping_{false};
};
