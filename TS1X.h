#ifndef TS1X_H
#define TS1X_H

#include <string>
#include <cstdint>

// Forward declarations
class SessionManager;
class CommandProcessor;
class Utility;

#include "pi_buffer.h"

#define RSSI_THRESHOLD -84
#define RSSI_DELAY 165
#define RSSI_INCREMENT 5
#define BROADCAST_INTERVAL 8


class CTS1X
{
public:
    CTS1X();
    virtual ~CTS1X();
    void rx_char(char);
    void go_main(bool verbose = false);

    typedef void (*FlushCallback)();
    void set_flush_callback(FlushCallback callback); 
    void flush_tx_buffer();      
    
    // Delegated methods
    void init_rf_channel();
    void init_utility();  
    void set_tx_buffer(pi_buffer* tx_buffer_ptr);
    
    // Thin wrapper - always send + print together
    void send_command(const unsigned char* cmd_buffer, int length);
    
    // Get managers
    SessionManager* get_session_manager() { return session_mgr; }
    CommandProcessor* get_command_processor() { return cmd_processor; }

    void scia_xmit(int ch);
    
    pi_buffer* command_buffer;

    int get_ibuf_count();


protected:
    int icnt;
    int ocnt;
    char* ibuf;
    int command_count;    
    SessionManager* session_mgr;
    CommandProcessor* cmd_processor;
    Utility* utility;
    pi_buffer* tx_buffer; 
    FlushCallback flush_callback;
};

#endif // TS1X_H