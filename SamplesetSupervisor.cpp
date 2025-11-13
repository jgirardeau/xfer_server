#include "SamplesetSupervisor.h"
#include "logger.h"

#include <sys/stat.h>
#include <algorithm>

SamplesetSupervisor::SamplesetSupervisor(const std::string& ts1x_config_path,
                                         const std::string& database_path)
    : ts1x_config_path_(ts1x_config_path),
      database_path_(database_path),
      db_manager_(nullptr),
      last_config_mtime_(0),
      last_reload_time_(0),
      reload_count_(0),
      initialized_(false),
      current_index_(0) {
    
    LOG_INFO_CTX("sampleset_super", "Creating SamplesetSupervisor");
    LOG_INFO_CTX("sampleset_super", "  Config file: %s", ts1x_config_path_.c_str());
    LOG_INFO_CTX("sampleset_super", "  Database file: %s", database_path_.c_str());
    
    // Create database manager
    db_manager_ = new SamplesetDataManager(database_path_);
}

SamplesetSupervisor::~SamplesetSupervisor() {
    LOG_INFO_CTX("sampleset_super", "Destroying SamplesetSupervisor - flushing database");
    
    // Final flush before shutdown
    if (db_manager_) {
        db_manager_->flush();
        delete db_manager_;
    }
}

bool SamplesetSupervisor::initialize() {
    LOG_INFO_CTX("sampleset_super", "Initializing SamplesetSupervisor");
    
    // Initialize database manager
    if (!db_manager_->initialize()) {
        LOG_ERROR_CTX("sampleset_super", "Failed to initialize database manager");
        return false;
    }
    
    LOG_INFO_CTX("sampleset_super", "Database initialized with %zu existing entries",
                 db_manager_->getEntryCount());
    
    // Load configuration and generate samplesets
    if (!load_and_generate()) {
        LOG_ERROR_CTX("sampleset_super", "Failed to load configuration");
        return false;
    }
    
    
    // Populate database with timestamps from API file
    populate_database_from_channels();
    // Refresh database to remove any stale entries
    int removed = db_manager_->refresh(samplesets_);
    if (removed > 0) {
        LOG_INFO_CTX("sampleset_super", "Removed %d stale entries from database", removed);
    }
    
    // Record initial file modification time
    last_config_mtime_ = get_config_file_mtime();
    last_reload_time_ = std::time(nullptr);
    reload_count_ = 0;
    initialized_ = true;
    
    // Initialize round-robin scheduler
    init_index();
    
    LOG_INFO_CTX("sampleset_super", "Initialization complete");
    LOG_INFO_CTX("sampleset_super", "  Channels: %zu", channels_.size());
    LOG_INFO_CTX("sampleset_super", "  Samplesets: %zu", samplesets_.size());
    LOG_INFO_CTX("sampleset_super", "  Database entries: %zu", db_manager_->getEntryCount());
    
    return true;
}

time_t SamplesetSupervisor::get_config_file_mtime() const {
    struct stat file_stat;
    if (stat(ts1x_config_path_.c_str(), &file_stat) == 0) {
        return file_stat.st_mtime;
    }
    return 0;
}

bool SamplesetSupervisor::load_and_generate() {
    LOG_INFO_CTX("sampleset_super", "Loading TS1X configuration from: %s",
                 ts1x_config_path_.c_str());
    
    // Load channels from file
    std::vector<Ts1xChannel> new_channels = readTs1xSamplingFile(ts1x_config_path_);
    
    if (new_channels.empty()) {
        LOG_WARN_CTX("sampleset_super", "No channels loaded from configuration file");
        return false;
    }
    
    // Generate samplesets
    std::vector<Sampleset> new_samplesets = createSamplesets(new_channels);
    
    if (new_samplesets.empty()) {
        //LOG_WARN_CTX("sampleset_super", "No samplesets generated from channels");
        return false;
    }
    
    // Update our state
    channels_ = std::move(new_channels);
    samplesets_ = std::move(new_samplesets);
    
    LOG_INFO_CTX("sampleset_super", "Loaded %zu channels, generated %zu samplesets",
                 channels_.size(), samplesets_.size());
    
    if (channels_.size() > samplesets_.size()) {
        LOG_INFO_CTX("sampleset_super", "Compression ratio: %zu channels -> %zu samplesets (%.1f%%)",
                     channels_.size(), samplesets_.size(),
                     (100.0 * samplesets_.size()) / channels_.size());
    }
    
    return true;
}

bool SamplesetSupervisor::check_and_reload_if_changed() {
    if (!initialized_) {
        LOG_WARN_CTX("sampleset_super", "check_and_reload_if_changed called before initialize()");
        return false;
    }
    
    // Get current modification time
    time_t current_mtime = get_config_file_mtime();
    
    if (current_mtime == 0) {
        LOG_ERROR_CTX("sampleset_super", "Failed to get config file modification time");
        return false;
    }
    
    // Check if file has changed
    if (current_mtime <= last_config_mtime_) {
        // No change
        return false;
    }
    
    LOG_INFO_CTX("sampleset_super", "Configuration file has changed - reloading");
    LOG_INFO_CTX("sampleset_super", "  Previous mtime: %ld", last_config_mtime_);
    LOG_INFO_CTX("sampleset_super", "  Current mtime: %ld", current_mtime);
    
    return reload_configuration();
}

bool SamplesetSupervisor::reload_configuration() {
    LOG_INFO_CTX("sampleset_super", "Reloading configuration");
    
    // Store old counts for comparison
    size_t old_channel_count = channels_.size();
    size_t old_sampleset_count = samplesets_.size();
    
    // Load new configuration
    if (!load_and_generate()) {
        LOG_ERROR_CTX("sampleset_super", "Failed to reload configuration");
        return false;
    }
    
    // Populate database with timestamps from API file
    populate_database_from_channels();
    
    // Refresh database to remove stale entries
    int removed = db_manager_->refresh(samplesets_);
    
    // Update tracking
    last_config_mtime_ = get_config_file_mtime();
    last_reload_time_ = std::time(nullptr);
    reload_count_++;
    
    // Flush database after reload
    flush_database();
    
    LOG_INFO_CTX("sampleset_super", "Configuration reloaded successfully");
    LOG_INFO_CTX("sampleset_super", "  Channels: %zu -> %zu", 
                 old_channel_count, channels_.size());
    LOG_INFO_CTX("sampleset_super", "  Samplesets: %zu -> %zu",
                 old_sampleset_count, samplesets_.size());
    LOG_INFO_CTX("sampleset_super", "  Stale entries removed: %d", removed);
    LOG_INFO_CTX("sampleset_super", "  Reload count: %d", reload_count_);
    
    return true;
}

bool SamplesetSupervisor::flush_database() {
    if (!db_manager_) {
        LOG_ERROR_CTX("sampleset_super", "Database manager not initialized");
        return false;
    }
    
    LOG_DEBUG_CTX("sampleset_super", "Flushing database to disk");
    bool result = db_manager_->flush();
    
    if (!result) {
        LOG_ERROR_CTX("sampleset_super", "Failed to flush database");
    }
    
    return result;
}

void SamplesetSupervisor::record_sample(const Sampleset& sampleset) {
    if (!db_manager_) {
        LOG_ERROR_CTX("sampleset_super", "Database manager not initialized");
        return;
    }
    
    db_manager_->recordSample(sampleset);
}

time_t SamplesetSupervisor::get_last_sample_time(const Sampleset& sampleset) const {
    if (!db_manager_) {
        return 0;
    }
    
    return db_manager_->getLastSampleTime(sampleset);
}

bool SamplesetSupervisor::has_been_sampled(const Sampleset& sampleset) const {
    if (!db_manager_) {
        return false;
    }
    
    return db_manager_->hasBeenSampled(sampleset);
}

size_t SamplesetSupervisor::get_database_entry_count() const {
    if (!db_manager_) {
        return 0;
    }
    
    return db_manager_->getEntryCount();
}

double SamplesetSupervisor::get_time_until_next_sample(const Sampleset& sampleset) const {
    if (!db_manager_) {
        return 0.0;
    }
    
    time_t last_sample = db_manager_->getLastSampleTime(sampleset);
    
    if (last_sample == 0) {
        // Never sampled - due now
        return 0.0;
    }
    
    time_t now = std::time(nullptr);
    double elapsed = difftime(now, last_sample);
    double time_until_next = sampleset.interval - elapsed;
    
    return time_until_next;
}

bool SamplesetSupervisor::is_due_for_sampling(const Sampleset& sampleset) const {
    if (!db_manager_) {
        return false;
    }
    
    time_t last_sample = db_manager_->getLastSampleTime(sampleset);
    
    if (last_sample == 0) {
        // Never sampled - due now
        return true;
    }
    
    time_t now = std::time(nullptr);
    double elapsed = difftime(now, last_sample);
    
    return elapsed >= sampleset.interval;
}

std::vector<Sampleset> SamplesetSupervisor::get_due_samplesets() const {
    std::vector<Sampleset> due_samplesets;
    
    for (const auto& sampleset : samplesets_) {
        if (is_due_for_sampling(sampleset)) {
            due_samplesets.push_back(sampleset);
        }
    }
    
    return due_samplesets;
}

void SamplesetSupervisor::print_samplesets() const {
    if (!samplesets_.empty()) {
        printSamplesets(samplesets_);
    }
}

SamplesetSupervisor::Statistics SamplesetSupervisor::get_statistics() const {
    Statistics stats;
    stats.channel_count = channels_.size();
    stats.sampleset_count = samplesets_.size();
    stats.database_entry_count = get_database_entry_count();
    stats.config_file_modified_time = last_config_mtime_;
    stats.last_reload_time = last_reload_time_;
    stats.reload_count = reload_count_;
    
    return stats;
}

void SamplesetSupervisor::init_index() {
    current_index_ = 0;
    LOG_INFO_CTX("sampleset_super", "Round-robin scheduler initialized to index 0");
}

const Sampleset* SamplesetSupervisor::get_sampleset() {
    if (!initialized_ || samplesets_.empty()) {
        //LOG_WARN_CTX("sampleset_super", "get_sampleset called but not initialized or no samplesets");
        return nullptr;
    }
    
    // Special case: only one sampleset
    if (samplesets_.size() == 1) {
        if (is_due_for_sampling(samplesets_[0])) {
            LOG_DEBUG_CTX("sampleset_super", "Single sampleset is due for sampling");
            return &samplesets_[0];
        } else {
            LOG_DEBUG_CTX("sampleset_super", "Single sampleset not yet due");
            return nullptr;
        }
    }
    
    // Start search at current_index
    size_t temp_index = current_index_;
    size_t sampleset_count = samplesets_.size();
    
    // Search through all samplesets in round-robin fashion
    for (size_t i = 0; i < sampleset_count; i++) {
        const Sampleset& sampleset = samplesets_[temp_index];
        
        // Check if this sampleset is due for sampling
        if (is_due_for_sampling(sampleset)) {
            // Found one! Advance current_index for next call
            current_index_ = (temp_index + 1) % sampleset_count;
            
            LOG_DEBUG_CTX("sampleset_super", 
                         "Found sampleset at index %zu (0x%08x mask=0x%02x) - advancing to %zu",
                         temp_index, sampleset.nodeid, sampleset.sampling_mask, current_index_);
            
            return &samplesets_[temp_index];
        }
        
        // Not due, advance to next sampleset
        temp_index = (temp_index + 1) % sampleset_count;
    }
    
    // Wrapped around - no samplesets need sampling
    //LOG_DEBUG_CTX("sampleset_super", "No samplesets need sampling (all up to date)");
    return nullptr;
}


void SamplesetSupervisor::populate_database_from_channels() {
    LOG_INFO_CTX("sampleset_super", "Populating database with timestamps from API file");
    
    if (!db_manager_) {
        LOG_ERROR_CTX("sampleset_super", "Database manager not initialized");
        return;
    }
    
    int populated = 0;
    int updated = 0;
    int skipped = 0;
    
    // For each sampleset, find the OLDEST last_sampled time from its contributing channels
    for (const auto& sampleset : samplesets_) {
        time_t oldest_time = 0;
        bool found_any = false;
        
        // Find all channels that contribute to this sampleset
        for (const auto& channel : channels_) {
            // Check if this channel matches the sampleset
            uint32_t channel_nodeid = 0;
            if (sscanf(channel.serial.c_str(), "0x%x", &channel_nodeid) != 1) {
                continue;
            }
            
            if (channel_nodeid != sampleset.nodeid) {
                continue;
            }
            
            // Check if channel type matches
            bool is_ac = (channel.channel_type == "AC");
            if (is_ac != (bool)sampleset.ac_dc_flag) {
                continue;
            }
            
            // Check if channel is in the sampling mask
            uint8_t channel_bit = (1 << channel.channel_num);
            if ((sampleset.sampling_mask & channel_bit) == 0) {
                continue;
            }
            
            // This channel contributes to this sampleset
            // Parse the last_sampled timestamp
            time_t channel_time = parse_timestamp(channel.last_sampled);
            
            if (channel_time == 0) {
                // Invalid or missing timestamp, skip
                continue;
            }
            
            // Track the oldest timestamp
            if (!found_any || channel_time < oldest_time) {
                oldest_time = channel_time;
                found_any = true;
            }
        }
        
        // If we found a valid timestamp, update the database
        if (found_any) {
            // Check if entry already exists
            bool already_exists = db_manager_->hasBeenSampled(sampleset);
            
            if (already_exists) {
                time_t existing_time = db_manager_->getLastSampleTime(sampleset);
                // Only update if API file has an older time (more conservative)
                if (oldest_time < existing_time) {
                    db_manager_->recordSample(sampleset, oldest_time);
                    updated++;
                    LOG_DEBUG_CTX("sampleset_super", 
                                 "Updated sampleset 0x%08x mask=0x%02x with older time from API",
                                 sampleset.nodeid, sampleset.sampling_mask);
                }
            } else {
                // New entry
                db_manager_->recordSample(sampleset, oldest_time);
                populated++;
                LOG_DEBUG_CTX("sampleset_super", 
                             "Populated new sampleset 0x%08x mask=0x%02x from API file",
                             sampleset.nodeid, sampleset.sampling_mask);
            }
        } else {
            // No valid timestamp found in API file for this sampleset
            skipped++;
        }
    }
    
    LOG_INFO_CTX("sampleset_super", 
                 "Database population complete: %d new, %d updated, %d skipped",
                 populated, updated, skipped);
    
    // Flush database to disk if we made changes
    if (populated > 0 || updated > 0) {
        flush_database();
    }
}

time_t SamplesetSupervisor::parse_timestamp(const std::string& timestamp_str) const {
    // API file format: "2025-10-25 22:10:11.000"
    // We'll parse up to seconds and ignore milliseconds
    
    if (timestamp_str.empty() || timestamp_str == "-") {
        return 0;
    }
    
    struct tm tm = {};
    // Try to parse the timestamp
    const char* result = strptime(timestamp_str.c_str(), "%Y-%m-%d %H:%M:%S", &tm);
    
    if (result == nullptr) {
        // Failed to parse
        LOG_DEBUG_CTX("sampleset_super", "Failed to parse timestamp: %s", timestamp_str.c_str());
        return 0;
    }
    
    // Convert to Unix timestamp
    time_t timestamp = mktime(&tm);
    if (timestamp == -1) {
        LOG_DEBUG_CTX("sampleset_super", "mktime() failed for timestamp: %s", timestamp_str.c_str());
        return 0;
    }
    
    return timestamp;
}
