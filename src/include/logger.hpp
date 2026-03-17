#pragma once

#include <chrono>
#include <format>
#include <iostream>
#include <mutex>
#include <string>
#include <vector>

#include "timing.hpp"

#define WITH_LOGGING

class Log {
  private:
    std::string content_;
    std::chrono::time_point<std::chrono::steady_clock> timeOfCreation_;

  public:
    Log(std::string cLog) : content_(cLog) { timeOfCreation_ = Timing::getTime(); }
    void print() {
        auto tp = timeOfCreation_.time_since_epoch(); // duration since steady_clock epoch
        auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(tp).count();
        std::cout << milliseconds << ": " << content_ << '\n';
    }
};

class Logger {
  private:
    std::vector<Log> logs_;
    std::mutex mtx_;

  public:
    void pushLog(Log log) {
        {
            std::lock_guard<std::mutex> lock(mtx_);
#ifdef WITH_LOGGING
            log.print();
#endif
        }
        logs_.emplace_back(log);
    }
    void clearOldLogs(std::size_t amount) {}
};