#ifndef RADIO_MANAGER_H
#define RADIO_MANAGER_H

#include <stdint.h>
#include "UartManager.h"

// GPIO Pins
#define PIRESETA 5
#define PICMDA 12
#define PIBEA 22
#define PICTS 6

// Radio defaults
#define DEFAULT_POWER_LEVEL 7
#define DEFAULT_CHANNEL 0

class RadioManager {
private:
    UartManager* uart;
    int radio_error;
    uint8_t current_rf_channel;
    uint8_t current_rf_tx_power;
    volatile int interrupt_count;

    // GPIO and hardware control
    bool init_gpio();
    void wait_on_cts();
    void wait_on_be();
    void set_command_mode();
    void clr_command_mode();

    // Radio communication
    void flush_radio();
    void wait_on_radio(int expect);
    void radio_command_mode(int id);
    void radio_command(char addr, char dat);
    char read_radio(char addr, char id);

public:
    RadioManager(UartManager* uart_mgr);
    ~RadioManager();

    // Initialization and configuration
    bool start();
    bool check_radio();
    void periodic_radio_check();

    // Channel and power control
    bool set_channel(uint8_t channel);
    bool set_tx_power(uint8_t power);
    uint8_t get_channel() const { return current_rf_channel; }
    uint8_t get_tx_power() const { return current_rf_tx_power; }

    // Status
    int get_error() const { return radio_error; }
    void clear_error() { radio_error = 0; }

    // Signal handling (called from interrupt)
    void handle_uart_interrupt();
    void increment_interrupt_count() { interrupt_count++; }
    void reset_interrupt_count() { interrupt_count = 0; }
    int get_interrupt_count() const { return interrupt_count; }
    
    // GPIO access for main
    void wait_on_buffer_empty() { wait_on_be(); }
};

#endif // RADIO_MANAGER_H