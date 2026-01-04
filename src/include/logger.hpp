#pragma once

#include <chrono>
#include <format>
#include <iostream>
#include <mutex>
#include <string>
#include <vector>

#include "timing.hpp"

class Log {
   private:
    std::string content;
    std::chrono::time_point<std::chrono::steady_clock> timeOfCreation;

   public:
    Log(std::string cLog) : content(cLog) { timeOfCreation = Timing::getTime(); }
    void print() {
        auto tp = timeOfCreation.time_since_epoch();  // duration since steady_clock epoch
        auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(tp).count();
        std::cout << milliseconds << ": " << content << '\n';
    }
};

class Logger {
   private:
    std::vector<Log> logs;
    std::mutex mtx;

   public:
    void pushLog(Log log) {
        {
            std::lock_guard<std::mutex> lock(mtx);
            log.print();
        }
        logs.emplace_back(log);
    }
    void clearOldLogs(std::size_t amount) {}
};