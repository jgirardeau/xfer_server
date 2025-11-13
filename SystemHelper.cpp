#include "SystemHelper.h"
#include "pi_buffer.h"
#include "UartManager.h"
#include "RadioManager.h"
#include "logger.h"
#include <sys/time.h>

SystemHelper::SystemHelper(UartManager* uart_mgr, RadioManager* radio_mgr)
    : uart_manager(uart_mgr), 
      radio_manager(radio_mgr),
      buffer_modulo(0)
{
    radio_check_tstamp = std::chrono::system_clock::now();
}

SystemHelper::~SystemHelper()
{
}

void SystemHelper::check_uart(pi_buffer* tx_buffer, pi_buffer* rx_buffer, pi_buffer* cmd_buffer)
{
    // Handle TX
    while (!tx_buffer->empty()) {
        char ch = tx_buffer->get_char();
        uart_manager->transmit_char(ch);
        buffer_modulo++;
        if (buffer_modulo == 128) {
            buffer_modulo = 0;
            radio_manager->wait_on_buffer_empty();
        }
    }

    // Handle RX - transfer from UART manager to rx_buffer
    while (uart_manager->get_input_count() != uart_manager->get_output_count()) {
        char ch = uart_manager->get_input_char();
        rx_buffer->add_char(ch);
    }

    // Handle commands
    bool radio_change = false;
    char radio_setting = 0;
    while (!cmd_buffer->empty()) {
        radio_setting = cmd_buffer->get_char();
        radio_change = true;
    }

    if (radio_change && (radio_setting & 0xc0) == 0x80) {
        int chan = radio_setting & 0x7;
        if (chan >= 0 && chan <= 5) {
            radio_manager->set_channel(chan);
        }
    } else if (radio_change && (radio_setting & 0xc0) == 0xc0) {
        int pow = radio_setting & 0x7;
        if (pow >= 5 && pow <= 7) {
            radio_manager->set_tx_power(pow);
        }
    }
}

void SystemHelper::setup_timer(long int useconds)
{
    struct itimerval timer;
    timer.it_interval.tv_usec = useconds;
    timer.it_interval.tv_sec = 0;
    timer.it_value.tv_usec = useconds;
    timer.it_value.tv_sec = 0;
    setitimer(ITIMER_REAL, &timer, NULL);
}

void SystemHelper::reset_radio_check_timestamp()
{
    radio_check_tstamp = std::chrono::system_clock::now();
}

uint64_t SystemHelper::get_seconds_since_last_radio_check()
{
    auto curr_time = std::chrono::system_clock::now();
    auto elapsed_seconds = std::chrono::duration_cast<std::chrono::seconds>(
        curr_time - radio_check_tstamp);
    return elapsed_seconds.count();
}

void SystemHelper::handle_uart_interrupt()
{
    if (radio_manager) {
        radio_manager->handle_uart_interrupt();
        radio_manager->increment_interrupt_count();
    }
}