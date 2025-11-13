#include "TS1X.h"
#include "CommandProcessor.h"
#include "Utility.h"
#include "SessionManager.h"
#include "logger.h"
#include "buffer_constants.h"
#define MAX_BUFFER_SIMULATION 65536*32

CTS1X::CTS1X()
{
    ibuf = new char[IBUF_MAX];
    command_count = 0;
    icnt = 0;
    ocnt = 0;
    command_buffer = nullptr;  // Initialize to nullptr
    tx_buffer=nullptr;
    
    // Initialize helpers - pass nullptr for command_buffer, will be set later
    utility = nullptr;  // Don't create yet
    cmd_processor = new CommandProcessor(this, ibuf, &icnt, &ocnt);
    
    // Enable upload data printing (set to false for production)
    cmd_processor->set_print_upload_data(true);
    
    // Initialize session manager
    session_mgr = new SessionManager(this);

    LOG_INFO_CTX("ts1x_core", "Initialize TS1X core");
}

CTS1X::~CTS1X()
{
    delete[] ibuf;
    delete session_mgr;
    delete cmd_processor;
    delete utility;
}

void CTS1X::rx_char(char ch)
{
    //printf("add %x: ts1x icnt/ocnt %d,%d\n",ch, icnt,ocnt);
    utility->rx_char(ch);
    //printf("   icnt/ocnt %d,%d\n",icnt,ocnt);
}

void CTS1X::init_rf_channel()
{
    utility->init_rf_channel();
}

void CTS1X::send_command(const unsigned char* cmd_buffer, int length)
{
    cmd_processor->send_command(cmd_buffer, length);
    cmd_processor->print_tx_command(cmd_buffer, length);
}

int CTS1X::get_ibuf_count(){
    int sv_delta = icnt - ocnt;
    if (sv_delta < 0){
        sv_delta += IBUF_MAX;
    } 
    return sv_delta;
}

void CTS1X::go_main(bool m_verbose)
{
    int sv_delta = get_ibuf_count();

    if (sv_delta < CLENG) {
        if (session_mgr) {
            session_mgr->process(nullptr);  // No response
        }
        return;
    }

    // Warning if buffer is getting full
    if (sv_delta > (int)(((float)IBUF_MAX) * 0.8)) {  // 80% full
        LOG_WARN_CTX("ts1x_core","RX buffer is %d%% full (%d/%d bytes)", 
                 (sv_delta * 100) / IBUF_MAX, sv_delta, IBUF_MAX);
    }

    // Check if we have a valid command header
    if (utility->is_valid_command_header()) {
        command_count++;

        cmd_processor->print_command();

        CommandResponse parsed_response = cmd_processor->parse_response();
        
        if (parsed_response.packet_valid ){
            // Device is alive but no data
            LOG_INFO_CTX("ts1x_core", "Node 0x%08x alive", parsed_response.source_macid);
            cmd_processor->print_response(parsed_response);
            if (session_mgr) {
                session_mgr->process(&parsed_response);
            }
        }
        else{
            session_mgr->process(nullptr);
        }
        //printf("accept command; old/new cnt %d, ", get_ibuf_count());
        utility->move_buffer(CLENG);
        //printf("%d\n", get_ibuf_count());
    } else {
        // Not a valid command, move forward by 1 and try again
        char trash_char=ibuf[ocnt];
        //printf("trash char old/new cnt %d, ", get_ibuf_count());
        utility->move_buffer(1);
        //printf("%d char %02x (%c)\n", get_ibuf_count(),trash_char, isprint(trash_char) ? trash_char : '.');

        // Process session state machine without response
        if (session_mgr) {
            session_mgr->process(nullptr);
        }
    }
}

void CTS1X::init_utility()
{
    if (!utility && command_buffer) {
        utility = new Utility(ibuf, &icnt, &ocnt, command_buffer);
    }
}

void CTS1X::scia_xmit(int ch)
{
    if (tx_buffer && !tx_buffer->full()) {
        tx_buffer->add_char(static_cast<char>(ch));
    }
    else if (!tx_buffer) {
        LOG_ERROR_CTX("ts1x_core", "TX buffer not initialized!");
    }
    else {
        LOG_ERROR_CTX("ts1x_core", "TX buffer full!");
    }
}

void CTS1X::set_tx_buffer(pi_buffer* tx_buffer_ptr){
    tx_buffer=tx_buffer_ptr;
}


void CTS1X::set_flush_callback(FlushCallback callback) {
    this->flush_callback = callback;
}

void CTS1X::flush_tx_buffer() {
    if (flush_callback && tx_buffer) {
        flush_callback();
    }
}