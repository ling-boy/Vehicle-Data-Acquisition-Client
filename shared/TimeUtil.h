#pragma once

#include <string>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace vehicle {

class TimeUtil {
public:
    static std::string getCurrentDateTimeString() {
        auto now  = std::chrono::system_clock::now();
        auto ms   = std::chrono::duration_cast<std::chrono::milliseconds>(
                        now.time_since_epoch()) % 1000;
        auto timer = std::chrono::system_clock::to_time_t(now);
        std::tm bt{};
        localtime_r(&timer, &bt);

        std::ostringstream oss;
        oss << std::put_time(&bt, "%Y-%m-%d %H-%M-%S")
            << '-' << std::setfill('0') << std::setw(3) << ms.count();
        return oss.str();
    }

    static std::string getCurrentDateString() {
        auto now   = std::chrono::system_clock::now();
        auto timer = std::chrono::system_clock::to_time_t(now);
        std::tm bt{};
        localtime_r(&timer, &bt);

        std::ostringstream oss;
        oss << std::put_time(&bt, "%Y-%m-%d");
        return oss.str();
    }

    static int64_t currentTimeMillis() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::system_clock::now().time_since_epoch()).count();
    }

    static Timestamp now() {
        return std::chrono::steady_clock::now();
    }

    static int64_t elapsedMs(Timestamp start, Timestamp end) {
        return std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    }
};

} // namespace vehicle
