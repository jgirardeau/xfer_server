#include "ConfigManager.h"

#include <cctype>
#include <fstream>
#include <sstream>

// ---------- singleton ----------
ConfigManager& ConfigManager::instance() {
    static ConfigManager inst;
    return inst;
}

// ---------- string helpers ----------
void ConfigManager::trim_inplace(std::string& s) {
    // handle all-whitespace (or empty) safely
    size_t first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) { s.clear(); return; }
    size_t last  = s.find_last_not_of(" \t\r\n");
    s.erase(last + 1);
    s.erase(0, first);
}

// Keys typically shouldn’t contain spaces; trim both sides fully.
void ConfigManager::trim_key_inplace(std::string& s) {
    trim_inplace(s);
}

// Values can have inner spaces; we still trim leading/trailing whitespace.
void ConfigManager::trim_value_inplace(std::string& s) {
    trim_inplace(s);
}

// ---------- load ----------
bool ConfigManager::load(const std::string& path) {
    std::ifstream in(path);
    if (!in.is_open()) {
        loaded_ = false;
        kv_.clear();
        return false;
    }

    kv_.clear();
    std::string line;
    int lineno = 0;

    while (std::getline(in, line)) {
        ++lineno;

        // Remove any trailing '\r' (Windows line endings)
        if (!line.empty() && line.back() == '\r') line.pop_back();

        // Strip comments that start a line
        std::string trimmed = line;
        trim_inplace(trimmed);
        if (trimmed.empty()) continue;
        if (trimmed[0] == '#') continue;

        // OPTIONAL: support inline comments: key=value # comment
        // (Uncomment next block if you want inline # comments.)
        // auto hash_pos = trimmed.find('#');
        // if (hash_pos != std::string::npos) {
        //     trimmed.erase(hash_pos);
        //     trim_inplace(trimmed);
        //     if (trimmed.empty()) continue;
        // }

        // Split on first '='
        auto eq = trimmed.find('=');
        if (eq == std::string::npos) {
            // no '=' -> skip malformed line
            continue;
        }

        std::string key = trimmed.substr(0, eq);
        std::string val = trimmed.substr(eq + 1);

        trim_key_inplace(key);
        trim_value_inplace(val);

        if (key.empty()) continue;  // ignore empty keys
        kv_[key] = val;             // last one wins
    }

    loaded_ = true;
    return true;
}

// ---------- getters ----------
std::string ConfigManager::get(const std::string& key, const std::string& default_value) const {
    auto it = kv_.find(key);
    return (it != kv_.end()) ? it->second : default_value;
}

int ConfigManager::get(const std::string& key, int default_value) const {
    auto it = kv_.find(key);
    if (it == kv_.end()) return default_value;

    // Robust integer parse; ignore leading/trailing spaces already trimmed
    int out = default_value;
    std::istringstream ss(it->second);
    ss >> out;
    if (!ss.fail()) return out;
    return default_value; // fallback on parse failure
}

bool ConfigManager::get(const std::string& key, bool default_value) const {
    auto it = kv_.find(key);
    if (it == kv_.end()) return default_value;
    
    std::string val = it->second;
    // Convert to lowercase for comparison
    for (char& c : val) {
        c = std::tolower(c);
    }
    
    // Accept: true, 1, yes, on
    if (val == "true" || val == "1" || val == "yes" || val == "on") {
        return true;
    }
    // Accept: false, 0, no, off
    if (val == "false" || val == "0" || val == "no" || val == "off") {
        return false;
    }
    
    return default_value; // fallback on unrecognized value
}
