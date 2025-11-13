#ifndef SAMPLESETSUPERVISOR_H
#define SAMPLESETSUPERVISOR_H

#include "SamplesetGenerator.h"
#include "SamplesetDataManager.h"
#include "Ts1xSamplingReader.h"
#include <string>
#include <vector>
#include <ctime>

/**
 * SamplesetSupervisor - Central management for sampleset configuration and history
 * 
 * Responsibilities:
 * - Load and parse TS1X sampling configuration file
 * - Generate samplesets from channel configurations
 * - Manage the persistent sampleset database (sample times)
 * - Watch for configuration file changes and auto-reload
 * - Handle periodic database saves
 * - Provide clean interface for main.cpp and SessionManager
 * 
 * This supervisor keeps main.cpp clean and provides SessionManager with
 * easy access to sampleset information and sampling history.
 */
class SamplesetSupervisor {
public:
    /**
     * Constructor
     * @param ts1x_config_path Path to TS1X sampling configuration file
     * @param database_path Path to sampleset database file
     */
    SamplesetSupervisor(const std::string& ts1x_config_path,
                       const std::string& database_path);
    
    ~SamplesetSupervisor();
    
    /**
     * Initialize the supervisor:
     * - Load TS1X configuration file
     * - Generate initial samplesets
     * - Initialize database from disk
     * - Refresh database to remove stale entries
     * 
     * @return true if successful, false on critical error
     */
    bool initialize();
    
    /**
     * Check if configuration file has changed and reload if necessary.
     * This monitors the file modification time and triggers a reload
     * when changes are detected.
     * 
     * @return true if file was reloaded, false if no change
     */
    bool check_and_reload_if_changed();
    
    /**
     * Force reload of configuration regardless of file timestamp.
     * Useful for manual refresh requests or initial load.
     * 
     * @return true if successful, false on error
     */
    bool reload_configuration();
    
    /**
     * Flush database to disk immediately.
     * Call periodically (e.g., every hour) or before shutdown.
     * 
     * @return true if successful, false on error
     */
    bool flush_database();
    
    /**
     * Record that a sampleset was sampled at the current time
     * @param sampleset The sampleset that was just sampled
     */
    void record_sample(const Sampleset& sampleset);
    
    /**
     * Get the last time a sampleset was sampled
     * @param sampleset The sampleset to query
     * @return Timestamp of last sample, or 0 if never sampled
     */
    time_t get_last_sample_time(const Sampleset& sampleset) const;
    
    /**
     * Check if a sampleset has ever been sampled
     * @param sampleset The sampleset to check
     * @return true if has been sampled at least once
     */
    bool has_been_sampled(const Sampleset& sampleset) const;
    
    /**
     * Get current list of active samplesets
     * @return Reference to the sampleset vector
     */
    const std::vector<Sampleset>& get_samplesets() const { return samplesets_; }
    
    /**
     * Get current list of TS1X channels
     * @return Reference to the channel vector
     */
    const std::vector<Ts1xChannel>& get_channels() const { return channels_; }
    
    /**
     * Get count of active samplesets
     */
    size_t get_sampleset_count() const { return samplesets_.size(); }
    
    /**
     * Get count of database entries (samplesets that have been sampled)
     */
    size_t get_database_entry_count() const;
    
    /**
     * Calculate time until next sample is due for a sampleset
     * @param sampleset The sampleset to check
     * @return Seconds until next sample (0 if due now, negative if overdue)
     */
    double get_time_until_next_sample(const Sampleset& sampleset) const;
    
    /**
     * Check if a sampleset is due for sampling based on its interval
     * @param sampleset The sampleset to check
     * @return true if sampling is due or overdue
     */
    bool is_due_for_sampling(const Sampleset& sampleset) const;
    
    /**
     * Get all samplesets that are due for sampling
     * @return Vector of samplesets ready to be sampled
     */
    std::vector<Sampleset> get_due_samplesets() const;
    
    /**
     * Print current sampleset configuration to logs
     */
    void print_samplesets() const;
    
    /**
     * Get statistics about the supervisor state
     */
    struct Statistics {
        size_t channel_count;
        size_t sampleset_count;
        size_t database_entry_count;
        time_t config_file_modified_time;
        time_t last_reload_time;
        int reload_count;
    };
    Statistics get_statistics() const;
    
    /**
     * Initialize the round-robin index to start at the first sampleset.
     * Call this after initialization or when you want to reset the scheduler.
     */
    void init_index();
    
    /**
     * Get the next sampleset that needs sampling using round-robin scheduling.
     * 
     * This function scans through samplesets starting at the current index,
     * looking for one that:
     * - Has never been sampled, OR
     * - Has been sampled but interval has elapsed
     * 
     * Returns nullptr if no sampleset needs sampling (all are up to date).
     * On success, advances current_index for the next call.
     * 
     * @return Pointer to the next sampleset needing sampling, or nullptr if none
     */
    const Sampleset* get_sampleset();
    
private:
    /**
     * Get the modification time of the configuration file
     * @return Modification timestamp, or 0 on error
     */
    time_t get_config_file_mtime() const;
    
    /**
     * Load channels from configuration file and generate samplesets
     * @return true if successful
     */
    bool load_and_generate();
    
    /**
     * Populate database with last_sampled times from parsed channels.
     * This extracts timestamp information from the API file and initializes
     * the database entries for existing samplesets.
     */
    void populate_database_from_channels();
    
    /**
     * Parse a timestamp string from the API file format.
     * @param timestamp_str Timestamp in format "YYYY-MM-DD HH:MM:SS.mmm"
     * @return Unix epoch time, or 0 if parsing fails
     */
    time_t parse_timestamp(const std::string& timestamp_str) const;
    
    std::string ts1x_config_path_;     // Path to TS1X sampling config file
    std::string database_path_;        // Path to sampleset database file
    
    std::vector<Ts1xChannel> channels_;  // Current channel configuration
    std::vector<Sampleset> samplesets_;  // Current samplesets
    
    SamplesetDataManager* db_manager_;   // Database manager
    
    time_t last_config_mtime_;         // Last known modification time of config file
    time_t last_reload_time_;          // Last time we reloaded the config
    int reload_count_;                 // Number of times config has been reloaded
    
    bool initialized_;                 // Whether initialize() has been called
    
    size_t current_index_;             // Current position in round-robin scheduler
};

#endif // SAMPLESETSUPERVISOR_H
