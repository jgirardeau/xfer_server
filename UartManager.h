#ifndef UART_MANAGER_H
#define UART_MANAGER_H

#include <termios.h>

// UART buffer settings
#define RXUARTBUFF 1024
#define UART_IBUF_MASK 0xFFF
#define UART_IBUF_MAX 4096

class UartManager {
private:
    int uart_filestream;
    char input_buffer[UART_IBUF_MAX];
    int input_count;
    int output_count;

    bool setup_serial_baudrate(unsigned int baud, bool standard_rate);

public:
    UartManager();
    ~UartManager();

    bool open_port(unsigned int baud, bool standard_rate = true);
    void close_port();
    bool is_open() const { return uart_filestream >= 0; }
    int get_fd() const { return uart_filestream; }

    // Buffer access
    int get_input_count() const { return input_count; }
    int get_output_count() const { return output_count; }
    char get_input_char();
    void reset_buffers();

    // UART operations
    void transmit_char(char ch);
    void transmit_bytes(int length, char* data);
    int receive_bytes();
    void flush_buffers();
};

#endif // UART_MANAGER_H