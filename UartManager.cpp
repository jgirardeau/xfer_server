#include "UartManager.h"
#include "logger.h"
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/serial.h>
#include <string.h>
#include <cctype>

UartManager::UartManager() 
    : uart_filestream(-1), input_count(0), output_count(0) {
    memset(input_buffer, 0, UART_IBUF_MAX);
}

UartManager::~UartManager() {
    close_port();
}

bool UartManager::setup_serial_baudrate(unsigned int baud, bool standard_rate) {
    if (uart_filestream == -1)
        return false;

    if (standard_rate) {
        struct serial_struct serinfo;
        if (ioctl(uart_filestream, TIOCGSERIAL, &serinfo) < 0)
            return false;

        serinfo.flags &= ~ASYNC_SPD_MASK;
        if (serinfo.custom_divisor != 0) {
            serinfo.custom_divisor = 0;
            serinfo.reserved_char[0] = 0;
            if (ioctl(uart_filestream, TIOCSSERIAL, &serinfo) < 0)
                return false;
        }

        struct termios options;
        tcgetattr(uart_filestream, &options);
        options.c_cflag = baud | CS8 | CLOCAL | CREAD;
        options.c_iflag = IGNPAR;
        options.c_oflag = 0;
        options.c_lflag = 0;
        tcflush(uart_filestream, TCIFLUSH);
        tcsetattr(uart_filestream, TCSANOW, &options);
        return true;
    } else {
        struct serial_struct serinfo;
        int rate = baud;
        if (ioctl(uart_filestream, TIOCGSERIAL, &serinfo) < 0)
            return false;

        serinfo.flags &= ~ASYNC_SPD_MASK;
        serinfo.flags |= ASYNC_SPD_CUST;
        serinfo.custom_divisor = (serinfo.baud_base + (rate / 2)) / rate;
        if (serinfo.custom_divisor < 1)
            serinfo.custom_divisor = 1;

        serinfo.reserved_char[0] = 0;
        if (ioctl(uart_filestream, TIOCSSERIAL, &serinfo) < 0)
            return false;

        struct termios options;
        tcgetattr(uart_filestream, &options);
        cfsetispeed(&options, B38400);
        cfsetospeed(&options, B38400);
        options.c_cflag |= CS8 | CLOCAL | CREAD;
        options.c_iflag = IGNPAR;
        options.c_oflag = 0;
        options.c_lflag = 0;
        tcflush(uart_filestream, TCIFLUSH);
        tcsetattr(uart_filestream, TCSANOW, &options);
        return true;
    }
}

bool UartManager::open_port(unsigned int baud, bool standard_rate) {
    if (uart_filestream >= 0) {
        close_port();
    }

    uart_filestream = open("/dev/serial0", O_RDWR | O_NOCTTY | O_NDELAY);
    if (uart_filestream == -1) {
        LOG_ERROR_CTX("uart_manager", "Error - Unable to open UART. Ensure it is not in use by another application");
        return false;
    }

    if (ioctl(uart_filestream, TIOCEXCL) < 0) {
        LOG_ERROR_CTX("uart_manager", "Unable to lock serial port");
        close(uart_filestream);
        uart_filestream = -1;
        return false;
    }

    LOG_INFO_CTX("uart_manager", "uart open %d", uart_filestream);

    if (!setup_serial_baudrate(baud, standard_rate)) {
        LOG_ERROR_CTX("uart_manager", "Unable to configure UART baud rate");
        close(uart_filestream);
        uart_filestream = -1;
        return false;
    }

    LOG_INFO_CTX("uart_manager", "serial port opened id %d", uart_filestream);
    return true;
}

void UartManager::close_port() {
    if (uart_filestream >= 0) {
        tcflush(uart_filestream, TCIOFLUSH);
        close(uart_filestream);
        uart_filestream = -1;
    }
}

void UartManager::transmit_char(char ch) {
    if (uart_filestream < 0)
        return;

    unsigned char tx_buffer[2];
    tx_buffer[0] = ch;
    int count = write(uart_filestream, tx_buffer, 1);
    if (count < 0) {
        LOG_ERROR_CTX("uart_manager", "UART TX error");
    }
    else {
        //tcdrain(uart_filestream);  // ← Wait until byte is transmitted
    }
}

void UartManager::transmit_bytes(int length, char* data) {
    for (int i = 0; i < length; i++) {
        transmit_char(data[i]);
    }
}

int UartManager::receive_bytes() {
    if (uart_filestream == -1)
        return 0;

    unsigned char rx_buffer[RXUARTBUFF];
    int rx_length = read(uart_filestream, (void*)rx_buffer, RXUARTBUFF);

    if (rx_length > 0) {
        for (int i = 0; i < rx_length; i++) {
            char ch=rx_buffer[i];
            //printf("uart add to rx buffer (%d,%d) %d: %02x (%c)\n",input_count,output_count,i,ch,isprint(ch) ? ch : '.');
            input_buffer[input_count++] = ch;
            input_count &= UART_IBUF_MASK;
        }
    }
    return rx_length;
}

char UartManager::get_input_char() {
    char ch = input_buffer[output_count++];
    //printf("uart take from rx buffer (%d,%d) %x\n",input_count,output_count,ch);
    output_count &= UART_IBUF_MASK;
    return ch;
}


void UartManager::reset_buffers() {
    input_count = 0;
    output_count = 0;
}

void UartManager::flush_buffers() {
    if (uart_filestream >= 0) {
        tcflush(uart_filestream, TCIOFLUSH);
    }
}