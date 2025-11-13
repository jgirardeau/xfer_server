// pi_server_sleep.h
#ifndef PI_SERVER_SLEEP_H
#define PI_SERVER_SLEEP_H

#include <thread>
#include <chrono>

// Clear, consistent API
inline void Server_sleep_ms(int ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

inline void Server_sleep_us(int us) {
    std::this_thread::sleep_for(std::chrono::microseconds(us));
}

inline void Server_sleep_sec(int sec) {
    std::this_thread::sleep_for(std::chrono::seconds(sec));
}

#endif