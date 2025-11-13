#include "RadioManager.h"
#include "logger.h"
#include <bcm2835.h>
#include <unistd.h>
#include <termios.h>
#include <signal.h>
#include "pi_server_sleep.h"

RadioManager::RadioManager(UartManager* uart_mgr)
    : uart(uart_mgr), radio_error(0), 
      current_rf_channel(DEFAULT_CHANNEL), 
      current_rf_tx_power(DEFAULT_POWER_LEVEL),
      interrupt_count(0) {
}

RadioManager::~RadioManager() {
    clr_command_mode();
    bcm2835_close();
}


bool RadioManager::init_gpio() {
    if (!bcm2835_init()) {
        LOG_ERROR_CTX("radio_manager", "FAIL TO INIT BCM2835");
        return false;
    }

    bcm2835_gpio_fsel(PIBEA, BCM2835_GPIO_FSEL_INPT);
    bcm2835_gpio_fsel(PICTS, BCM2835_GPIO_FSEL_INPT);
    bcm2835_gpio_fsel(PICMDA, BCM2835_GPIO_FSEL_OUTP);
    bcm2835_gpio_set(PICMDA);
    bcm2835_gpio_fsel(PIRESETA, BCM2835_GPIO_FSEL_OUTP);
    bcm2835_gpio_clr(PIRESETA);
    bcm2835_delayMicroseconds(500000);
    bcm2835_gpio_set(PIRESETA);

    return true;
}

void RadioManager::wait_on_cts() {
    while (bcm2835_gpio_lev(PICTS));
}

void RadioManager::wait_on_be() {
    while (!bcm2835_gpio_lev(PIBEA));
}

void RadioManager::set_command_mode() {
    bcm2835_gpio_clr(PICMDA);
    bcm2835_delayMicroseconds(100000);
}

void RadioManager::clr_command_mode() {
    bcm2835_gpio_set(PICMDA);
    bcm2835_delayMicroseconds(100000);
}

void RadioManager::flush_radio() {
    bool flush = false;
    while (!flush) {
        uart->reset_buffers();
        bcm2835_delayMicroseconds(100000);
        if (uart->get_input_count() == 0) flush = true;
    }
}

void RadioManager::wait_on_radio(int expect) {
    interrupt_count = 0;
    while (uart->get_input_count() < expect && interrupt_count < 4)
        bcm2835_delayMicroseconds(100000);
}

void RadioManager::radio_command_mode(int id) {
    if (id) {
        if (uart->is_open()) {
            bcm2835_delayMicroseconds(10000);
            uart->flush_buffers();
            bcm2835_delayMicroseconds(10000);
        }
        set_command_mode();
        if (uart->is_open()) {
            uart->flush_buffers();
        }
    } else {
        clr_command_mode();
    }
}

void RadioManager::radio_command(char addr, char dat) {
    char rcmd[4];
    LOG_INFO_CTX("radio_manager", "Write radio reg %x to %x", addr, dat);
    flush_radio();
    radio_command_mode(1);
    uart->reset_buffers();
    rcmd[0] = 0xff;
    rcmd[1] = 0x2;
    rcmd[2] = addr;
    rcmd[3] = dat;
    
    for (int i = 0; i < 4; i++) {
        wait_on_cts();
        uart->transmit_char(rcmd[i]);
    }
    
    wait_on_radio(1);
    if (uart->get_input_count() != 1 || uart->get_input_char() != 0x6) {
        radio_error = 1;
    }
    radio_command_mode(0);
}

char RadioManager::read_radio(char addr, char id) {
    flush_radio();
    char rcmd[4];
    radio_command_mode(1);
    uart->reset_buffers();
    rcmd[0] = 0xff;
    rcmd[1] = 0x2;
    rcmd[2] = 0xfe;
    rcmd[3] = addr;
    
    for (int i = 0; i < 4; i++) {
        wait_on_cts();
        uart->transmit_char(rcmd[i]);
    }
    
    wait_on_radio(10);

    char result = 0;
    if (uart->get_input_count() >= 3) {
        char ack = uart->get_input_char();
        char addr_echo = uart->get_input_char();
        result = uart->get_input_char();
        
        if (ack != 0x6 || addr_echo != addr) {
            radio_error = 1;
        }
    } else {
        radio_error = 1;
    }
    
    radio_command_mode(0);
    return result;
}

bool RadioManager::check_radio() {
    const struct {
        uint8_t addr;
        uint8_t expected_val;
    } check_regs[] = {
        {0x4d, DEFAULT_POWER_LEVEL},
        {0x4e, 0x05}, // baud rate
        {0x4f, 0x04}, // addressing mode
        {0x50, 0x02}, // data timeout
        {0x53, 0x01}, // enable CRC check
        {0x54, 0x90}, // byte count trigger
        {0x56, 0x01}, // enable CSMA
        {0x58, 0x00}, // idle mode
        {0x70, 0x00}, // compatibility
        {0x6e, 0x01}, // command mode hold
        {0xd3, 0x00}, // packet options
    };

    bool ret = true;
    for (size_t reg_idx = 0; reg_idx < sizeof(check_regs) / sizeof(check_regs[0]); reg_idx++) {
        radio_error = 0;
        int val = read_radio(check_regs[reg_idx].addr, 1);
        if (radio_error) {
            LOG_ERROR_CTX("radio_manager", "Unable to check register %02x", check_regs[reg_idx].addr);
            continue;
        }
        if ((val & 0xff) != check_regs[reg_idx].expected_val) {
            LOG_ERROR_CTX("radio_manager", "Radio reg %02x val %02x expected %02x",
                check_regs[reg_idx].addr, val & 0xFF, check_regs[reg_idx].expected_val);
            ret = false;
        } else {
            LOG_INFO_CTX("radio_manager", "Confirmed reg %02x val %02x", check_regs[reg_idx].addr, val & 0xFF);
        }
    }
    return ret;
}

bool RadioManager::start() {
    // Retry GPIO init
    int gpio_init_retry_count = 0;
    while (gpio_init_retry_count < 10) {
        if (init_gpio())
            break;
        Server_sleep_sec(1);
        gpio_init_retry_count++;
    }

    if (gpio_init_retry_count >= 10) {
        LOG_ERROR_CTX("radio_manager", "Unable to init GPIO controller");
        return false;
    }

    uart->open_port(B9600);
    interrupt_count = 0;
    while (interrupt_count <= 25);

    // Try to read baud rate
    radio_error = 0;
    LOG_INFO_CTX("radio_manager", "start of 1st read");
    int baud = read_radio(0x4e, 1);
    LOG_INFO_CTX("radio_manager", "first read %d %d", baud, radio_error);

    bool retval = false;

    if (!radio_error && baud == 1) {
        retval = true;
    } else {
        // Try different baud rates
        const struct {
            int radio_baud_id;
            bool std_flag;
            int baud_rate;
            const char *desc;
        } radio_baud_rates[] = {
            {5, true, B115200, "115200"},
            {2, true, B19200, "19200"},
            {3, true, B38400, "38400"},
            {4, true, B57600, "57600"},
            {6, false, 10400, "10400"},
            {7, false, 31250, "31250"},
        };

        for (size_t baud_idx = 0; baud_idx < sizeof(radio_baud_rates) / sizeof(radio_baud_rates[0]); baud_idx++) {
            uart->open_port(radio_baud_rates[baud_idx].baud_rate, radio_baud_rates[baud_idx].std_flag);
            radio_error = 0;
            LOG_INFO_CTX("radio_manager", "trying baud rate %s", radio_baud_rates[baud_idx].desc);
            baud = read_radio(0x4e, 1);
            LOG_INFO_CTX("radio_manager", "read %d %d", baud, radio_error);

            if (!radio_error) {
                retval = true;
                radio_command(0x3, 0x1); // set nv ram to 9600
                break;
            }
        }
    }

    if (!retval) {
        LOG_INFO_CTX("radio_manager", "Fail and leave");
        Server_sleep_sec(1);
        return retval;
    }

    if (baud != 5) { // not 115200
        LOG_INFO_CTX("radio_manager", "set radio to 115200");
        radio_command(0x4e, 0x5);
        uart->open_port(B115200);
        bcm2835_delayMicroseconds(100000);
        LOG_INFO_CTX("radio_manager", "read baud rate again");
        radio_error = 0;
        baud = read_radio(0x4e, 1);
        if (!radio_error && baud == 5) {
            LOG_INFO_CTX("radio_manager", "baud rate is set OK");
        } else {
            LOG_INFO_CTX("radio_manager", "baud rate set fails");
            return false;
        }
    }

    // Program registers
    const struct {
        uint8_t addr;
        uint8_t val;
    } prog_regs[] = {
        {0x4d, DEFAULT_POWER_LEVEL},
        {0x4b, DEFAULT_CHANNEL},
        {0x4f, 0x04}, {0x50, 0x02}, {0x53, 0x01}, {0x54, 0x90},
        {0x56, 0x01}, {0x58, 0x00}, {0x70, 0x00}, {0x6e, 0x01},
        {0xd3, 0x00},
    };

    for (size_t reg_idx = 0; reg_idx < sizeof(prog_regs) / sizeof(prog_regs[0]); reg_idx++) {
        radio_error = 0;
        bcm2835_delayMicroseconds(20000);
        radio_command(prog_regs[reg_idx].addr, prog_regs[reg_idx].val);
        if (radio_error) {
            LOG_ERROR_CTX("radio_manager", "Unable to program register %02x", prog_regs[reg_idx].addr);
            retval = false;
        }
    }

    if (!retval) {
        LOG_ERROR_CTX("radio_manager", "radio programming failed");
        return false;
    }

    retval = check_radio();

    // NV registers - check and update if necessary
    if (retval) {
        const struct {
            uint8_t addr;
            uint8_t val;
        } check_prog_regs[] = {
            {0x3f, 0xba}, // min carrier RSSI for CSMA
            {0x23, 0x01}, // Command mode hold
        };

        for (size_t reg_idx = 0; reg_idx < sizeof(check_prog_regs) / sizeof(check_prog_regs[0]); reg_idx++) {
            radio_error = 0;
            int val = read_radio(check_prog_regs[reg_idx].addr, 1);
            if (radio_error) {
                LOG_ERROR_CTX("radio_manager", "Unable to check register %02x", check_prog_regs[reg_idx].addr);
                retval = false;
            } else if ((val & 0xff) != check_prog_regs[reg_idx].val) {
                LOG_ERROR_CTX("radio_manager", "Unexpected value of register %02x", check_prog_regs[reg_idx].addr);
                radio_error = 0;
                radio_command(check_prog_regs[reg_idx].addr, check_prog_regs[reg_idx].val);
                if (radio_error) {
                    LOG_ERROR_CTX("radio_manager", "Unable to program register %02x", check_prog_regs[reg_idx].addr);
                    retval = false;
                }
            } else {
                LOG_INFO_CTX("radio_manager", "Confirmed reg %02x", check_prog_regs[reg_idx].addr);
            }
        }
    }

    LOG_INFO_CTX("radio_manager", "return value %d", retval);
    return retval;
}

void RadioManager::periodic_radio_check() {
    int mismatch_count = 0;
    int err_count = 0;

    const struct {
        uint8_t addr;
        uint8_t val;
    } check_prog_regs[] = {
        {0x4d, current_rf_tx_power},
        {0x4b, current_rf_channel},
        {0x4f, 0x04}, {0x50, 0x02}, {0x53, 0x01}, {0x54, 0x90},
        {0x56, 0x01}, {0x58, 0x00}, {0x70, 0x00}, {0x6e, 0x01},
        {0xd3, 0x00}, {0x3f, 0xba},
    };

    LOG_INFO_CTX("radio_manager", "Periodic radio check...");
    for (size_t reg_idx = 0; reg_idx < sizeof(check_prog_regs) / sizeof(check_prog_regs[0]); reg_idx++) {
        radio_error = 0;
        int read_val = read_radio(check_prog_regs[reg_idx].addr, 1);
        if (radio_error) {
            err_count++;
            LOG_ERROR_CTX("radio_manager", "Unable to check register %02x", check_prog_regs[reg_idx].addr);
            continue;
        }
        if ((read_val & 0xff) != check_prog_regs[reg_idx].val) {
            mismatch_count++;
            LOG_ERROR_CTX("radio_manager", "Radio reg %02x val %02x expected %02x",
                check_prog_regs[reg_idx].addr, read_val & 0xFF, check_prog_regs[reg_idx].val);

            radio_command(check_prog_regs[reg_idx].addr, check_prog_regs[reg_idx].val);
            if (radio_error) {
                err_count++;
                LOG_ERROR_CTX("radio_manager", "Unable to program register %02x", check_prog_regs[reg_idx].addr);
            }
        } else {
            LOG_INFO_CTX("radio_manager", "Confirmed reg %02x val %02x", check_prog_regs[reg_idx].addr, read_val & 0xFF);
        }
    }
}

bool RadioManager::set_channel(uint8_t channel) {
    if (channel >= 0 && channel <= 5) {
        LOG_INFO_CTX("radio_manager", "radio channel change %d", channel);
        radio_command(0x4b, channel);
        current_rf_channel = channel;
        return radio_error == 0;
    }
    return false;
}

bool RadioManager::set_tx_power(uint8_t power) {
    if (power >= 5 && power <= 7) {
        LOG_INFO_CTX("radio_manager", "radio power change %d", power);
        radio_command(0x4d, power);
        current_rf_tx_power = power;
        return radio_error == 0;
    }
    return false;
}

void RadioManager::handle_uart_interrupt() {
    uart->receive_bytes();
}
