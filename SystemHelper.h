#ifndef SYSTEMHELPER_H
#define SYSTEMHELPER_H

#include <chrono>
#include <cstdint>

class pi_buffer;
class UartManager;
class RadioManager;

class SystemHelper {
public:
    SystemHelper(UartManager* uart_mgr, RadioManager* radio_mgr);
    ~SystemHelper();
    
    // UART and buffer processing
    void check_uart(pi_buffer* tx_buffer, pi_buffer* rx_buffer, pi_buffer* cmd_buffer);
    
    // Timer setup
    void setup_timer(long int useconds);
    
    // Radio check timing
    void reset_radio_check_timestamp();
    uint64_t get_seconds_since_last_radio_check();
    
    // Signal handler helpers
    void handle_uart_interrupt();
    
private:
    UartManager* uart_manager;
    RadioManager* radio_manager;
    int buffer_modulo;
    std::chrono::time_point<std::chrono::system_clock> radio_check_tstamp;
};

#endif // SYSTEMHELPER_H