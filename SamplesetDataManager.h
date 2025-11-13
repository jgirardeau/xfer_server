#ifndef SAMPLESETDATAMANAGER_H
#define SAMPLESETDATAMANAGER_H

#include "SamplesetGenerator.h"
#include <string>
#include <map>
#include <ctime>
#include <vector>

/**
 * Manages a persistent database of sampleset sampling times.
 * 
 * Keeps track of when each sampleset was last sampled, stored in RAM for fast access
 * and periodically persisted to disk. The database is keyed by a unique identifier
 * derived from the sampleset's characteristics (nodeid, mask, type, freq, etc.).
 * 
 * This allows detection of stale entries when sampleset configurations change.
 */
class SamplesetDataManager {
public:
    /**
     * Constructor
     * @param database_path Path to the persistent database file
     *                      e.g., "/srv/UPTIMEDRIVE/wvsh/sampleset_times.txt"
     */
    explicit SamplesetDataManager(const std::string& database_path);
    
    /**
     * Destructor - automatically flushes any pending changes to disk
     */
    ~SamplesetDataManager();
    
    /**
     * Initialize the manager by loading existing data from disk
     * @return true if successful (or file doesn't exist yet), false on error
     */
    bool initialize();
    
    /**
     * Refresh the database with current sampleset list.
     * Removes any entries that no longer correspond to valid samplesets.
     * Does NOT add new entries - those are added only when sampled.
     * 
     * @param current_samplesets The current active sampleset configuration
     * @return Number of stale entries removed
     */
    int refresh(const std::vector<Sampleset>& current_samplesets);
    
    /**
     * Record that a sampleset has been sampled at the current time
     * @param sampleset The sampleset that was just sampled
     * @param timestamp Optional timestamp (default = current time)
     */
    void recordSample(const Sampleset& sampleset, time_t timestamp = 0);
    
    /**
     * Get the last sample time for a sampleset
     * @param sampleset The sampleset to query
     * @return Last sample timestamp, or 0 if never sampled
     */
    time_t getLastSampleTime(const Sampleset& sampleset) const;
    
    /**
     * Check if a sampleset has ever been sampled
     * @param sampleset The sampleset to check
     * @return true if there's a record of this sampleset being sampled
     */
    bool hasBeenSampled(const Sampleset& sampleset) const;
    
    /**
     * Flush in-memory data to disk immediately
     * @return true if successful, false on error
     */
    bool flush();
    
    /**
     * Get the number of samplesets currently tracked
     * @return Count of entries in the database
     */
    size_t getEntryCount() const { return sample_times_.size(); }
    
    /**
     * Clear all entries from the database (RAM and disk)
     * Useful for testing or reset scenarios
     */
    void clear();
    
private:
    /**
     * Generate a unique string key for a sampleset based on its characteristics.
     * Format: "nodeid_mask_acdc_maxfreq_resolution_interval"
     * Example: "0x00111578_0x03_DC_0.0_0_10.0"
     * 
     * Note: Priority is NOT included as it's a scheduling hint, not a sampling characteristic
     */
    static std::string generateKey(const Sampleset& sampleset);
    
    /**
     * Load database from disk file
     * @return true if successful (or file doesn't exist), false on error
     */
    bool loadFromFile();
    
    /**
     * Save database to disk file
     * @return true if successful, false on error
     */
    bool saveToFile();
    
    std::string database_path_;                    // Path to persistent storage file
    std::map<std::string, time_t> sample_times_;   // In-memory database: key -> timestamp
    bool dirty_;                                    // Flag indicating unsaved changes
};

#endif // SAMPLESETDATAMANAGER_H
