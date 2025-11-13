#ifndef CONFIGMANAGER_H
#define CONFIGMANAGER_H

#include <string>
#include <unordered_map>

class ConfigManager {
public:
    static ConfigManager& instance();

    // Load "key=value" pairs from a text file. Lines starting with '#' are ignored.
    // Returns true on success (file opened and parsed).
    bool load(const std::string& path);

    // Generic typed getters
    std::string get(const std::string& key, const std::string& default_value) const;
    int         get(const std::string& key, int default_value) const;
    bool        get(const std::string& key, bool default_value) const;

    // Convenience getters used by main.cpp (adapt defaults as you like)
    std::string get_version() const {
        return get("system.version", std::string("unknown"));
    }
    std::string get_ping_file() const {
        return get("system.ping_file", std::string("/tmp/ping.txt"));
    }
    int get_radio_check_period_seconds() const {
        return get("session.radio_check_period_seconds", 5);
    }
    int get_pi_buffer_size() const {
        return get("session.pi_buffer_size", 4096);
    }
    int get_command_buffer_size() const {
        return get("session.command_buffer_size", 1024);
    }
    int get_timer_interval_us() const {
        return get("session.timer_interval_us", 20000);
    }
    int get_main_loop_delay_us() const {
        return get("session.main_loop_delay_us", 20000);
    }

    int get_response_timeout_ms() const {
    return get("session.response_timeout_ms", 3000);
    }

    int get_max_retry_count() const {
        return get("session.max_retry_count", 3);
    }
    
    // Nodelist directory configuration
    std::string get_nodelist_directory() const {
        return get("session.nodelist_directory", std::string("/srv/UPTIMEDRIVE/nodelist"));
    }
    
    // Node list file (built from nodelist directory + filename)
    std::string get_node_list_file() const {
        return get_nodelist_directory() + "/nodelist_force.txt";
    }

    // File output configuration
    std::string get_root_filehandler() const {
        return get("output.root_filehandler", std::string("/tmp/filehandler"));
    }
    
    std::string get_ts1_data_files() const {
        return get("ts1_data_files", std::string("/tmp/ts1_data_files"));
    }
    
    std::string get_ts1x_sampling_file() const {
        return get("ts1x_sampling_file", std::string("/srv/UPTIMEDRIVE/wvsh/api_ts1x_sampling.txt"));
    }
    
    std::string get_sampleset_database_file() const {
        return get("sampleset_database_file", std::string("/srv/UPTIMEDRIVE/wvsh/sampleset_times.txt"));
    }
    
    std::string get_config_files_directory() const {
        return get("config.files_directory", std::string("/srv/UPTIMEDRIVE/commands"));
    }
    
    // Log directory configuration
    std::string get_log_directory() const {
        return get("system.log_directory", std::string("/srv/UPTIMEDRIVE/logs"));
    }
    
    // Sensor configuration
    bool get_clip_negative_temperatures() const {
        return get("sensor.clip_negative_temperatures", true);
    }
    
    // Upload configuration
    int get_upload_packet_timeout_ms() const {
        return get("upload.packet_timeout_ms", 1000);
    }
    
    int get_upload_max_retry_count() const {
        return get("upload.max_retry_count", 10);
    }
    
    int get_upload_retry_timeout_ms() const {
        return get("upload.retry_timeout_ms", 1000);
    }

    bool is_loaded() const { return loaded_; }

private:
    ConfigManager() = default;

    // helpers
    static void trim_inplace(std::string& s);
    static void trim_key_inplace(std::string& s);
    static void trim_value_inplace(std::string& s);

    std::unordered_map<std::string, std::string> kv_;
    bool loaded_ = false;
};

#endif // CONFIGMANAGER_H