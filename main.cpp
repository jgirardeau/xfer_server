// main.cpp
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <string>
#include <thread>
#include <cctype>
#include <fstream>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <vector>
#include <unistd.h>
#include <signal.h>
#include <sys/time.h>

#include "ConfigManager.h"
#include "logger.h"
#include "TS1X.h"
#include "pi_buffer.h"
#include "UartManager.h"
#include "RadioManager.h"
#include "SessionManager.h"
#include "pi_server_sleep.h"
#include "buffer_constants.h"
#include "SamplesetSupervisor.h"
#include "MainLoopConstants.h"

using namespace std;

//
const auto VERSION = std::string("1.0.0");

// ===== Command-line options =====
struct CommandLineOptions {
    bool monitor_mode = false;
    bool show_help = false;
    std::string config_file = "./config.txt";
};

// ===== Globals (needed for signal handlers) =====
static std::atomic<bool> g_running{true};
static UartManager*  g_uart_manager  = nullptr;
static RadioManager* g_radio_manager = nullptr;
SamplesetSupervisor* g_sampleset_supervisor = nullptr;

// ===== Help text =====
static void print_help(const char* program_name) {
    printf("\nUsage: %s [OPTIONS]\n", program_name);
    printf("\nOptions:\n");
    printf("  --monitor         Enable monitor/listen-only mode (no TX, no config broadcast)\n");
    printf("  --config FILE     Specify config file path (default: ./config.txt)\n");
    printf("  --help            Display this help message and exit\n");
    printf("\nDescription:\n");
    printf("  Pi Server - Wireless sensor network base station\n");
    printf("\n");
    printf("Monitor Mode:\n");
    printf("  When --monitor is specified, the system operates in receive-only mode:\n");
    printf("    - No data upload responses are processed\n");
    printf("    - No configuration broadcasts are sent\n");
    printf("    - Useful for passive monitoring and debugging\n");
    printf("\n");
    printf("Examples:\n");
    printf("  %s                              # Normal operation with default config\n", program_name);
    printf("  %s --config /path/to/config.txt # Use custom config file\n", program_name);
    printf("  %s --monitor                    # Monitor mode with default config\n", program_name);
    printf("  %s --monitor --config custom.txt # Monitor mode with custom config\n", program_name);
    printf("\n");
}

// make buffer pointers global
pi_buffer* rx_buffer;
pi_buffer* tx_buffer;
pi_buffer* cmd_buffer;

// ===== Command-line parsing =====
static bool parse_command_line(int argc, char** argv, CommandLineOptions& options) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "--help" || arg == "-h") {
            options.show_help = true;
            return true;
        }
        else if (arg == "--monitor") {
            options.monitor_mode = true;
        }
        else if (arg == "--config") {
            if (i + 1 < argc) {
                options.config_file = argv[++i];
            } else {
                fprintf(stderr, "Error: --config requires a file path argument\n");
                return false;
            }
        }
        else {
            fprintf(stderr, "Error: Unknown option '%s'\n", arg.c_str());
            return false;
        }
    }
    return true;
}

// ===== Helpers =====
static void handle_sigterm(int) {
    LOG_INFO("SIGTERM received. Flushing database and closing UART...");
    if (g_sampleset_supervisor) {
        g_sampleset_supervisor->flush_database();
    }
    if (g_uart_manager) g_uart_manager->close_port();
    _exit(0);
}

static void handle_sigalrm(int) {
    if (g_radio_manager) {
        g_radio_manager->handle_uart_interrupt();
        g_radio_manager->increment_interrupt_count();
    }
}

static inline bool is_power_of_two(int x) {
    return x > 0 && (x & (x - 1)) == 0;
}

static inline bool file_exists_readable(const std::string& p) {
    std::ifstream f(p);
    return f.good();
}

static void timer_useconds(long int usec) {
    struct itimerval t{};
    t.it_interval.tv_sec  = 0;
    t.it_interval.tv_usec = usec;
    t.it_value.tv_sec     = 0;
    t.it_value.tv_usec    = usec;
    setitimer(ITIMER_REAL, &t, nullptr);
}

// ===== Config validation (sane ranges, existence checks) =====
static bool validate_config(const ConfigManager& cfg) {
    bool ok = true;

    // system.*
    const std::string ping_file     = cfg.get("system.ping_file", std::string("/tmp/ping.txt"));
    const int radio_sec             = cfg.get("system.radio_check_period_seconds", 28800);
    const int pi_buf_sz             = cfg.get("system.pi_buffer_size", 1048576);
    const int cmd_buf_sz            = cfg.get("system.command_buffer_size", 16);
    const std::string rf_chan_file  = cfg.get("system.rf_channel_file", std::string("/home/pi/channel.txt"));

    if (!file_exists_readable(rf_chan_file)) {
        LOG_WARN("system.rf_channel_file not readable: %s", rf_chan_file.c_str());
    }
    if (radio_sec < RADIO_CHECK_MIN_SEC || radio_sec > RADIO_CHECK_MAX_SEC) {
        LOG_ERROR("system.radio_check_period_seconds=%d out of range [%d..%d]", 
                  radio_sec, RADIO_CHECK_MIN_SEC, RADIO_CHECK_MAX_SEC); 
        ok = false;
    }
    if (pi_buf_sz < PI_BUFFER_MIN_SIZE || pi_buf_sz > PI_BUFFER_MAX_SIZE) {
        LOG_ERROR("system.pi_buffer_size=%d out of range [%d..%d]", 
                  pi_buf_sz, PI_BUFFER_MIN_SIZE, PI_BUFFER_MAX_SIZE); 
        ok = false;
    } else if (!is_power_of_two(pi_buf_sz)) {
        LOG_WARN("system.pi_buffer_size=%d not a power of two (ring buffers faster with pow2)", pi_buf_sz);
    }
    if (cmd_buf_sz < CMD_BUFFER_MIN_SIZE || cmd_buf_sz > CMD_BUFFER_MAX_SIZE) {
        LOG_ERROR("system.command_buffer_size=%d out of range [%d..%d]", 
                  cmd_buf_sz, CMD_BUFFER_MIN_SIZE, CMD_BUFFER_MAX_SIZE); 
        ok = false;
    }

    // uart.*
    const int timer_us = cfg.get("uart.timer_interval_us", 5000);
    const int loop_us  = cfg.get("uart.main_loop_delay_us", 10000);
    if (timer_us < TIMER_INTERVAL_MIN_US || timer_us > TIMER_INTERVAL_MAX_US) {
        LOG_ERROR("uart.timer_interval_us=%d out of range [%d..%d]", 
                  timer_us, TIMER_INTERVAL_MIN_US, TIMER_INTERVAL_MAX_US); 
        ok = false;
    }
    if (loop_us < LOOP_DELAY_MIN_US || loop_us > LOOP_DELAY_MAX_US) {
        LOG_ERROR("uart.main_loop_delay_us=%d out of range [%d..%d]", 
                  loop_us, LOOP_DELAY_MIN_US, LOOP_DELAY_MAX_US); 
        ok = false;
    }

    // Config broadcasting parameters
    const std::string config_dir = cfg.get("config_files_directory", 
                                          std::string("/srv/UPTIMEDRIVE/commands"));
    const int rssi_threshold = cfg.get("global_mistlx_rssi_threshold", RSSI_THRESHOLD);
    const int rssi_delay = cfg.get("global_mistlx_rssi_delay", RSSI_DELAY);
    const int rssi_increment = cfg.get("global_mistlx_rssi_increment", RSSI_INCREMENT);
    const int power_adjust = cfg.get("poweradjust", 0);
    const int broadcast_interval = cfg.get("config_broadcast_interval_hours", BROADCAST_INTERVAL);

    if (rssi_threshold < RSSI_THRESHOLD_MIN || rssi_threshold > RSSI_THRESHOLD_MAX) {
        LOG_ERROR("global_mistlx_rssi_threshold=%d out of range [%d..%d]", 
                  rssi_threshold, RSSI_THRESHOLD_MIN, RSSI_THRESHOLD_MAX); 
        ok = false;
    }
    if (rssi_delay < RSSI_PARAM_MIN || rssi_delay > RSSI_PARAM_MAX) {
        LOG_ERROR("global_mistlx_rssi_delay=%d out of range [%d..%d]", 
                  rssi_delay, RSSI_PARAM_MIN, RSSI_PARAM_MAX); 
        ok = false;
    }
    if (rssi_increment < RSSI_PARAM_MIN || rssi_increment > RSSI_PARAM_MAX) {
        LOG_ERROR("global_mistlx_rssi_increment=%d out of range [%d..%d]", 
                  rssi_increment, RSSI_PARAM_MIN, RSSI_PARAM_MAX); 
        ok = false;
    }
    if (power_adjust < RSSI_PARAM_MIN || power_adjust > RSSI_PARAM_MAX) {
        LOG_ERROR("poweradjust=%d out of range [%d..%d]", 
                  power_adjust, RSSI_PARAM_MIN, RSSI_PARAM_MAX); 
        ok = false;
    }
    if (broadcast_interval < BROADCAST_INTERVAL_MIN_HOURS || broadcast_interval > BROADCAST_INTERVAL_MAX_HOURS) {
        LOG_ERROR("config_broadcast_interval_hours=%d out of range [%d..%d]", 
                  broadcast_interval, BROADCAST_INTERVAL_MIN_HOURS, BROADCAST_INTERVAL_MAX_HOURS); 
        ok = false;
    }
    
    // Check if config directory exists (warning only)
    struct stat st;
    if (stat(config_dir.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) {
        LOG_WARN("config_files_directory not found or not a directory: %s", config_dir.c_str());
        LOG_WARN("Config broadcasting will be disabled");
    }

    if (!ok) LOG_ERROR("Configuration invalid.");
    else     LOG_INFO("Configuration validated.");
    return ok;
}

// ===== UART + buffer service (TX/RX/CMD) =====
static int g_buffer_modulo = 0;



static void service_uart_tx_buffer(pi_buffer* tx_buffer){
  // TX: flush to UART; throttle with radio wait to avoid overrun
  while (!tx_buffer->empty()) {
        char ch = tx_buffer->get_char();
        g_uart_manager->transmit_char(ch);
        if (++g_buffer_modulo == 128) {
            g_buffer_modulo = 0;
            g_radio_manager->wait_on_buffer_empty();
        }
    }

}

static void service_uart_and_buffers(pi_buffer* tx_buffer,
                                     pi_buffer* rx_buffer,
                                     pi_buffer* cmd_buffer)
{

    service_uart_tx_buffer(tx_buffer);

    // RX: pull from UART into rx_buffer
    while (g_uart_manager->get_input_count() != g_uart_manager->get_output_count()) {
        char ch = g_uart_manager->get_input_char();
        rx_buffer->add_char(ch);
    }

    // CMD: last-wins semantics for radio settings
    bool radio_change = false;
    char radio_setting = 0;
    while (!cmd_buffer->empty()) {
        radio_setting = cmd_buffer->get_char();
        radio_change = true;
    }
    if (radio_change && (radio_setting & 0xC0) == 0x80) {
        int chan = radio_setting & 0x7;
        if (0 <= chan && chan <= 5) g_radio_manager->set_channel(chan);
    } else if (radio_change && (radio_setting & 0xC0) == 0xC0) {
        int pow = radio_setting & 0x7;
        if (5 <= pow && pow <= 7) g_radio_manager->set_tx_power(pow);
    }
}

static void service_uart_tx_buffer_callback(){
    service_uart_and_buffers(tx_buffer,rx_buffer,cmd_buffer);
}

int main(int argc, char** argv) {
    // ---- Parse command-line arguments ----
    CommandLineOptions options;
    if (!parse_command_line(argc, argv, options)) {
        print_help(argv[0]);
        return EXIT_FAILURE;
    }
    
    if (options.show_help) {
        print_help(argv[0]);
        return EXIT_SUCCESS;
    }

     // ---- Config first ----
    const std::string cfg_path = options.config_file;
    auto& cfg = ConfigManager::instance();
    if (!cfg.load(cfg_path)) {
        // Can't log yet, so use cerr
        std::cerr << "ERROR: Failed to load config file: " << cfg_path << std::endl;
        return EXIT_FAILURE;
    }

    // ---- Logger initialization with config ----
    std::string log_directory = cfg.get_log_directory();
    std::cout << "Initializing logger with directory: " << log_directory << std::endl;
    init_logger(log_directory);

    // Log operating mode
    if (options.monitor_mode) {
        LOG_INFO("========================================");
        LOG_INFO("MONITOR MODE ENABLED");
        LOG_INFO("  - No TX responses will be sent");
        LOG_INFO("  - No config broadcasts will be sent");
        LOG_INFO("  - Receive-only operation");
        LOG_INFO("========================================");
    }

    // Log the values we depend on
    LOG_INFO("Config loaded from: %s", cfg_path.c_str());
    LOG_INFO("system.version: %s", cfg.get("system.version", std::string(VERSION)).c_str());
    LOG_INFO("system.ping_file: %s", cfg.get("system.ping_file", std::string("/tmp/ping.txt")).c_str());
    LOG_INFO("system.radio_check_period_seconds: %d", cfg.get("system.radio_check_period_seconds", 28800));
    LOG_INFO("system.pi_buffer_size: %d", cfg.get("system.pi_buffer_size", 1048576));
    LOG_INFO("system.command_buffer_size: %d", cfg.get("system.command_buffer_size", 16));
    LOG_INFO("system.rf_channel_file: %s", cfg.get("system.rf_channel_file", std::string("/home/pi/channel.txt")).c_str());
    LOG_INFO("uart.timer_interval_us: %d", cfg.get("uart.timer_interval_us", 5000));
    LOG_INFO("uart.main_loop_delay_us: %d", cfg.get("uart.main_loop_delay_us", 10000));

    if (!validate_config(cfg)) return EXIT_FAILURE;

    // ---- Resolve runtime params from config ----
    const std::string PING_FILE = cfg.get("system.ping_file", std::string("/tmp/ping.txt"));
    const int RADIO_CHECK_PERIOD_SECONDS = cfg.get("system.radio_check_period_seconds", 28800);
    const int PI_BUFFER_SIZE  = cfg.get("system.pi_buffer_size", 1048576);
    const int CMD_BUFFER_SIZE = cfg.get("system.command_buffer_size", 16);
    const int TIMER_US        = cfg.get("uart.timer_interval_us", 5000);
    const int LOOP_US         = cfg.get("uart.main_loop_delay_us", 10000);

    // Create/refresh ping file at startup
    if (FILE* f = fopen(PING_FILE.c_str(), "w")) fclose(f);

    // ---- Signals & periodic timer ----
    signal(SIGTERM, &handle_sigterm);
    signal(SIGALRM, &handle_sigalrm);
    timer_useconds(TIMER_US);

    // ---- Managers & device init ----
    g_uart_manager  = new UartManager();
    g_radio_manager = new RadioManager(g_uart_manager);

    CTS1X* unit = new CTS1X;
    rx_buffer  = new pi_buffer(PI_BUFFER_SIZE);
    tx_buffer  = new pi_buffer(PI_BUFFER_SIZE);
    cmd_buffer = new pi_buffer(CMD_BUFFER_SIZE);
    unit->command_buffer  = cmd_buffer;
    unit->init_utility();
    unit->set_tx_buffer(tx_buffer);
    unit->set_flush_callback(service_uart_tx_buffer_callback);

    // ---- Initialize Config Broadcaster ----
    // SessionManager is already created inside CTS1X, so get it
    SessionManager* session_mgr = unit->get_session_manager();
    
    // Set monitor mode if requested
    if (options.monitor_mode) {
        session_mgr->set_monitor_mode(true);
    }
    
    // Load config broadcaster parameters
    std::string config_dir = cfg.get("config_files_directory", 
                                    std::string("/srv/UPTIMEDRIVE/commands"));
    signed char rssi_threshold = (signed char)cfg.get("global_mistlx_rssi_threshold", RSSI_THRESHOLD);
    unsigned char rssi_delay = (unsigned char)cfg.get("global_mistlx_rssi_delay", RSSI_DELAY);
    unsigned char rssi_increment = (unsigned char)cfg.get("global_mistlx_rssi_increment", RSSI_INCREMENT);
    unsigned char power_adjust = (unsigned char)cfg.get("poweradjust", 0);
    int broadcast_interval_hours = cfg.get("config_broadcast_interval_hours", BROADCAST_INTERVAL); 
    
    LOG_INFO("Config Broadcasting Parameters:");
    LOG_INFO("  config_files_directory: %s", config_dir.c_str());
    LOG_INFO("  global_mistlx_rssi_threshold: %d", (int)rssi_threshold);
    LOG_INFO("  global_mistlx_rssi_delay: %d", (int)rssi_delay);
    LOG_INFO("  global_mistlx_rssi_increment: %d", (int)rssi_increment);
    LOG_INFO("  poweradjust: %d", (int)power_adjust);
    LOG_INFO("  config_broadcast_interval_hours: %d", broadcast_interval_hours);
    
    // Initialize the config broadcaster
    session_mgr->initialize_config_broadcaster(
        config_dir,
        rssi_threshold,
        rssi_delay,
        rssi_increment,
        power_adjust,
        broadcast_interval_hours
    );

    // ---- Initialize SamplesetSupervisor ----
    LOG_INFO("Initializing sampleset management...");
    std::string ts1x_sampling_file = cfg.get_ts1x_sampling_file();
    std::string sampleset_database_file = cfg.get_sampleset_database_file();
    
    g_sampleset_supervisor = new SamplesetSupervisor(ts1x_sampling_file, 
                                                      sampleset_database_file);
    
    if (!g_sampleset_supervisor->initialize()) {
        LOG_ERROR("Failed to initialize sampleset supervisor");
        if(!options.monitor_mode)
            return EXIT_FAILURE;
    }
    
    // Display the loaded configuration
    g_sampleset_supervisor->print_samplesets();
    
    LOG_INFO("Sampleset supervisor initialized successfully");
    LOG_INFO("  Channels: %zu", g_sampleset_supervisor->get_channels().size());
    LOG_INFO("  Samplesets: %zu", g_sampleset_supervisor->get_sampleset_count());
    LOG_INFO("  Database entries: %zu", g_sampleset_supervisor->get_database_entry_count());

    LOG_INFO("Starting radio...");
    while (!g_radio_manager->start()) {
        Server_sleep_ms(RADIO_STARTUP_RETRY_DELAY_MS); // retry every 200 ms until radio ready
    }
    LOG_INFO("Radio is OK!");

    // ---- Main loop state ----
    auto radio_check_tstamp = std::chrono::system_clock::now();
    auto database_flush_tstamp = std::chrono::system_clock::now();
    auto config_check_tstamp = std::chrono::system_clock::now();
    int  modulo_counter     = 0;
    bool first_time_through = true;
    g_buffer_modulo         = 0;

    LOG_INFO("Startup complete. Entering main loop.");

    while (true) {
        // Periodic radio check
        auto now = std::chrono::system_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - radio_check_tstamp).count();
        if (elapsed >= RADIO_CHECK_PERIOD_SECONDS) {
            g_radio_manager->periodic_radio_check();
            radio_check_tstamp = std::chrono::system_clock::now();
        }

        // Periodic database flush (every hour)
        auto flush_elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - database_flush_tstamp).count();
        if (flush_elapsed >= DATABASE_FLUSH_INTERVAL_SEC) {  // 3600 seconds = 1 hour
            if (g_sampleset_supervisor) {
                LOG_INFO("Performing hourly database flush");
                g_sampleset_supervisor->flush_database();
                database_flush_tstamp = std::chrono::system_clock::now();
            }
        }
        
        // Check for config file changes (every 30 seconds)
        auto config_elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - config_check_tstamp).count();
        if (config_elapsed >= CONFIG_FILE_CHECK_INTERVAL_SEC) {  // Check every 30 seconds
            if (g_sampleset_supervisor) {
                if (g_sampleset_supervisor->check_and_reload_if_changed()) {
                    LOG_INFO("Configuration file changed - samplesets updated");
                    g_sampleset_supervisor->print_samplesets();
                }
                config_check_tstamp = std::chrono::system_clock::now();
            }
        }

        // First-time RF channel init (reads system.rf_channel_file)
        if (first_time_through) {
            first_time_through = false;
            unit->init_rf_channel();
        }

        // UART service: TX/RX/CMD
        service_uart_and_buffers(tx_buffer, rx_buffer, cmd_buffer);

        // Drain RX chars into TS1X
        int bcount=rx_buffer->get_count();
        //if(bcount)printf("drain %d bytes\n",bcount);
        for (int i=0;i<bcount;i++){
            if(!rx_buffer->empty()){
                 char ch = rx_buffer->get_char();
                //printf("Transfer data to ts1x unit: %x, buffer fullness %d\n",ch,unit->get_ibuf_count());
                unit->rx_char(ch);
                //printf("buffer cnt %d, ibuf cnt %d: %02x (%c)\n",rx_buffer->get_count(),unit->get_ibuf_count(),ch, isprint(ch) ? ch : '.');
            }
        }
        /*
        while (!rx_buffer->empty()) {
            char ch = rx_buffer->get_char();
            //printf("Transfer data to ts1x unit: %x, buffer fullness %d\n",ch,unit->get_ibuf_count());
            unit->rx_char(ch);
            //printf("buffer cnt %d, ibuf cnt %d: %02x (%c)\n",rx_buffer->get_count(),unit->get_ibuf_count(),ch, isprint(ch) ? ch : '.');
        }*/

        // TS1X main processing (includes SessionManager processing)
        //if(unit->get_ibuf_count()>=CLENG)
        //    LOG_INFO("go_main data count: %d",unit->get_ibuf_count());
        unit->go_main(true);
#ifdef DOOOOO
        int sv_delta = unit->get_ibuf_count();
        while(sv_delta>=CLENG){
            LOG_INFO("Additional RX buffer drain, count: %d",sv_delta);
            unit->go_main(true); // drain rx buffer
            sv_delta = unit->get_ibuf_count();
#endif

        // Periodic ping file touch
        if (!(modulo_counter++ % PING_FILE_UPDATE_MODULO)) {
            if (FILE* f = fopen(PING_FILE.c_str(), "w")) fclose(f);
        }

        // Loop delay
        if (LOOP_US > 0) Server_sleep_us(MAIN_LOOP_FALLBACK_DELAY_US);
        else             std::this_thread::yield();
    }

    // (Normally never reached)
    LOG_INFO("Shutting down - flushing database...");
    if (g_sampleset_supervisor) {
        g_sampleset_supervisor->flush_database();
        delete g_sampleset_supervisor;
        g_sampleset_supervisor = nullptr;
    }
    
    cleanup_logger(); 
    delete unit;
    delete rx_buffer;
    delete tx_buffer;
    delete cmd_buffer;
    delete g_radio_manager;
    delete g_uart_manager;
    return 0;
}
