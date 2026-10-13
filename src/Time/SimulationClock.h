#pragma once
#include <cmath>
#include <cstdint>
#include <stdexcept>

// 当前单线程游戏的模拟时钟；窗口与输入仍使用真实时间，游戏行为使用此时钟。
class SimulationClock {
    inline static double seconds = 0;
public:
    static double Seconds() { return seconds; }
    static std::uint32_t Milliseconds() { return static_cast<std::uint32_t>(seconds * 1000.0); }
    static void Set(double value) {
        if (!std::isfinite(value) || value < 0 || value > 4000000)
            throw std::invalid_argument("模拟时间超出有效范围");
        seconds = value;
    }
    static void Advance(double delta) { Set(seconds + delta); }
};
