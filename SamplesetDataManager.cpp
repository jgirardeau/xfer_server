#include "SamplesetDataManager.h"
#include "logger.h"

#include <fstream>
#include <sstream>
#include <cstdio>
#include <algorithm>
#include <set>

SamplesetDataManager::SamplesetDataManager(const std::string& database_path)
    : database_path_(database_path), dirty_(false) {
}

SamplesetDataManager::~SamplesetDataManager() {
    // Auto-flush on destruction if there are unsaved changes
    if (dirty_) {
        LOG_INFO_CTX("sampleset_db", "Auto-flushing database on destruction");
        flush();
    }
}

bool SamplesetDataManager::initialize() {
    LOG_INFO_CTX("sampleset_db", "Initializing SamplesetDataManager from: %s", 
                 database_path_.c_str());
    
    sample_times_.clear();
    dirty_ = false;
    
    // Try to load existing data; it's okay if the file doesn't exist yet
    bool result = loadFromFile();
    
    LOG_INFO_CTX("sampleset_db", "Loaded %zu sampleset entries from database", 
                 sample_times_.size());
    
    return result;
}

std::string SamplesetDataManager::generateKey(const Sampleset& sampleset) {
    // Format: nodeid_mask_acdc_maxfreq_resolution_interval
    // Example: "0x00111578_0x03_DC_0.0_0_10.0"
    // Note: Priority is excluded as it's a scheduling hint, not a sampling characteristic
    
    char buffer[256];
    snprintf(buffer, sizeof(buffer), "0x%08x_0x%02x_%s_%.1f_%d_%.1f",
             sampleset.nodeid,
             sampleset.sampling_mask,
             sampleset.ac_dc_flag ? "AC" : "DC",
             sampleset.max_freq,
             sampleset.resolution,
             sampleset.interval);
    
    return std::string(buffer);
}

bool SamplesetDataManager::loadFromFile() {
    std::ifstream file(database_path_);
    
    if (!file.is_open()) {
        // File doesn't exist yet - this is fine on first run
        LOG_INFO_CTX("sampleset_db", "Database file does not exist yet (first run?)");
        return true;
    }
    
    std::string line;
    int line_num = 0;
    int loaded = 0;
    int skipped = 0;
    
    while (std::getline(file, line)) {
        line_num++;
        
        // Skip empty lines and comments
        if (line.empty() || line[0] == '#') {
            continue;
        }
        
        // Parse: <key> <timestamp>
        std::istringstream iss(line);
        std::string key;
        time_t timestamp;
        
        if (iss >> key >> timestamp) {
            sample_times_[key] = timestamp;
            loaded++;
        } else {
            LOG_WARN_CTX("sampleset_db", "Failed to parse line %d: %s", line_num, line.c_str());
            skipped++;
        }
    }
    
    file.close();
    
    if (skipped > 0) {
        LOG_WARN_CTX("sampleset_db", "Loaded %d entries, skipped %d invalid lines", 
                     loaded, skipped);
    }
    
    return true;
}

bool SamplesetDataManager::saveToFile() {
    // Write to a temporary file first, then rename for atomicity
    std::string temp_path = database_path_ + ".tmp";
    
    std::ofstream file(temp_path);
    if (!file.is_open()) {
        LOG_ERROR_CTX("sampleset_db", "Failed to open database file for writing: %s", 
                      temp_path.c_str());
        return false;
    }
    
    // Write header
    file << "# Sampleset sampling times database\n";
    file << "# Format: <key> <timestamp>\n";
    file << "# Key format: nodeid_mask_acdc_maxfreq_resolution_interval\n";
    file << "# Timestamp: Unix epoch time\n";
    file << "#\n";
    
    // Write all entries
    for (const auto& entry : sample_times_) {
        file << entry.first << " " << entry.second << "\n";
    }
    
    file.close();
    
    // Atomic rename
    if (std::rename(temp_path.c_str(), database_path_.c_str()) != 0) {
        LOG_ERROR_CTX("sampleset_db", "Failed to rename temp file to database file");
        return false;
    }
    
    dirty_ = false;
    LOG_DEBUG_CTX("sampleset_db", "Saved %zu entries to database", sample_times_.size());
    
    return true;
}

int SamplesetDataManager::refresh(const std::vector<Sampleset>& current_samplesets) {
    LOG_INFO_CTX("sampleset_db", "Refreshing database with %zu current samplesets", 
                 current_samplesets.size());
    
    // Build a set of valid keys from current samplesets
    std::set<std::string> valid_keys;
    for (const auto& sampleset : current_samplesets) {
        std::string key = generateKey(sampleset);
        valid_keys.insert(key);
    }
    
    // Find and remove stale entries (entries not in the valid set)
    std::vector<std::string> stale_keys;
    for (const auto& entry : sample_times_) {
        if (valid_keys.find(entry.first) == valid_keys.end()) {
            stale_keys.push_back(entry.first);
        }
    }
    
    // Remove stale entries
    for (const auto& key : stale_keys) {
        sample_times_.erase(key);
        LOG_DEBUG_CTX("sampleset_db", "Removed stale entry: %s", key.c_str());
    }
    
    if (!stale_keys.empty()) {
        dirty_ = true;
        LOG_INFO_CTX("sampleset_db", "Removed %zu stale entries from database", 
                     stale_keys.size());
        
        // Flush changes to disk
        flush();
    } else {
        LOG_INFO_CTX("sampleset_db", "No stale entries found, database is up to date");
    }
    
    return static_cast<int>(stale_keys.size());
}

void SamplesetDataManager::recordSample(const Sampleset& sampleset, time_t timestamp) {
    // Use current time if timestamp not specified
    if (timestamp == 0) {
        timestamp = std::time(nullptr);
    }
    
    std::string key = generateKey(sampleset);
    
    // Check if this is a new entry or update
    bool is_new = (sample_times_.find(key) == sample_times_.end());
    
    sample_times_[key] = timestamp;
    dirty_ = true;
    
    if (is_new) {
        LOG_DEBUG_CTX("sampleset_db", "Recorded NEW sample: %s at timestamp %ld", 
                      key.c_str(), timestamp);
    } else {
        LOG_DEBUG_CTX("sampleset_db", "Updated sample time: %s at timestamp %ld", 
                      key.c_str(), timestamp);
    }
}

time_t SamplesetDataManager::getLastSampleTime(const Sampleset& sampleset) const {
    std::string key = generateKey(sampleset);
    
    auto it = sample_times_.find(key);
    if (it != sample_times_.end()) {
        return it->second;
    }
    
    return 0;  // Never sampled
}

bool SamplesetDataManager::hasBeenSampled(const Sampleset& sampleset) const {
    std::string key = generateKey(sampleset);
    return sample_times_.find(key) != sample_times_.end();
}

bool SamplesetDataManager::flush() {
    if (!dirty_) {
        LOG_DEBUG_CTX("sampleset_db", "Database is clean, no flush needed");
        return true;
    }
    
    LOG_INFO_CTX("sampleset_db", "Flushing %zu entries to disk", sample_times_.size());
    
    bool result = saveToFile();
    if (result) {
        LOG_INFO_CTX("sampleset_db", "Successfully flushed database to disk");
    } else {
        LOG_ERROR_CTX("sampleset_db", "Failed to flush database to disk");
    }
    
    return result;
}

void SamplesetDataManager::clear() {
    LOG_WARN_CTX("sampleset_db", "Clearing all database entries");
    
    sample_times_.clear();
    dirty_ = true;
    
    // Flush to write empty database
    flush();
}
