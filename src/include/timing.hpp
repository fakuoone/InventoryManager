#pragma once

#include <chrono>
#include <iostream>

namespace Timing {
inline std::chrono::time_point<std::chrono::steady_clock> start;
inline std::chrono::time_point<std::chrono::steady_clock> end;

inline std::chrono::time_point<std::chrono::steady_clock> getTime() {
    return std::chrono::steady_clock::now();
}

inline void startTimer() {
    start = std::chrono::steady_clock::now();
}

inline void endTimer() {
    end = std::chrono::steady_clock::now();
}
}