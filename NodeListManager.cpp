#include "NodeListManager.h"
#include "UnitType.h"
#include "logger.h"
#include <fstream>
#include <sstream>
#include <algorithm>

NodeListManager::NodeListManager()
    : current_node_index(0)
    , last_load_attempt(std::chrono::steady_clock::time_point())
{
}

NodeListManager::~NodeListManager()
{
}

void NodeListManager::set_node_list_file(const std::string& filename)
{
    nodelist_filename = filename;
    LOG_INFO_CTX("nodelist_mgr", "Node list file set to: %s", filename.c_str());
}

bool NodeListManager::load_node_list()
{
    if (nodelist_filename.empty()) {
        LOG_INFO_CTX("nodelist_mgr", "No node list file configured");
        return false;
    }
    
    std::ifstream file(nodelist_filename);
    if (!file.is_open()) {
        // Don't log error every time - caller handles retry logic
        return false;
    }
    
    node_list.clear();
    std::string line;
    int skipped_non_echobase = 0;
    
    while (std::getline(file, line)) {
        // Remove whitespace
        line.erase(std::remove_if(line.begin(), line.end(), ::isspace), line.end());
        
        if (line.empty() || line[0] == '#') continue;  // Skip empty lines and comments
        
        // Parse hex number
        uint32_t macid;
        std::stringstream ss;
        ss << std::hex << line;
        ss >> macid;
        
        if (!ss.fail()) {
            // Only add EchoBase nodes - filter out TS1X, StormX, etc.
            if (is_echobox(macid)) {
                node_list.push_back(NodeInfo(macid));
                LOG_INFO_CTX("nodelist_mgr", "Added EchoBase node: 0x%08x", macid);
            } else {
                skipped_non_echobase++;
                LOG_WARN_CTX("nodelist_mgr", "Skipped non-EchoBase node 0x%08x (type: %s)", 
                            macid, unit_type_to_string(get_unit_type(macid)));
            }
        }
    }
    
    file.close();
    
    // Reset to first node
    current_node_index = 0;
    last_load_attempt = std::chrono::steady_clock::now();
    
    LOG_INFO_CTX("nodelist_mgr", "Loaded %zu EchoBase nodes from %s", 
                 node_list.size(), nodelist_filename.c_str());
    if (skipped_non_echobase > 0) {
        LOG_WARN_CTX("nodelist_mgr", "Skipped %d non-EchoBase nodes", skipped_non_echobase);
    }
    
    return node_list.size() > 0;
}

bool NodeListManager::reload_node_list()
{
    LOG_INFO_CTX("nodelist_mgr", "Reloading node list...");
    
    size_t old_size = node_list.size();
    bool success = load_node_list();
    
    if (success) {
        LOG_INFO_CTX("nodelist_mgr", "Node list reloaded: %zu nodes (was %zu)", 
                     node_list.size(), old_size);
    }
    
    return success;
}

bool NodeListManager::has_current_node() const
{
    return !node_list.empty() && current_node_index < node_list.size();
}

uint32_t NodeListManager::get_current_macid() const
{
    if (!has_current_node()) {
        LOG_ERROR_CTX("nodelist_mgr", "Attempt to get MAC ID with no current node");
        return 0;
    }
    return node_list[current_node_index].macid;
}

NodeInfo* NodeListManager::get_current_node()
{
    if (!has_current_node()) {
        return nullptr;
    }
    return &node_list[current_node_index];
}

void NodeListManager::move_to_next_node()
{
    if (!node_list.empty()) {
        current_node_index++;
        
        if (current_node_index >= node_list.size()) {
            LOG_INFO_CTX("nodelist_mgr", "Reached end of node list");
        }
    }
}

void NodeListManager::reset_to_first_node()
{
    current_node_index = 0;
    LOG_INFO_CTX("nodelist_mgr", "Reset to first node");
}

bool NodeListManager::is_at_end() const
{
    return node_list.empty() || current_node_index >= node_list.size();
}

bool NodeListManager::should_attempt_load()
{
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        now - last_load_attempt).count();
    
    return elapsed >= LOAD_RETRY_INTERVAL_SECONDS;
}

bool NodeListManager::check_and_reload_if_at_end()
{
    if (is_at_end()) {
        bool success = reload_node_list();
        if (success) {
            reset_to_first_node();
        }
        return success;
    }
    return true;  // Not at end, no reload needed
}

NodeInfo* NodeListManager::find_node_by_macid(uint32_t macid)
{
    for (auto& node : node_list) {
        if (node.macid == macid) {
            return &node;
        }
    }
    return nullptr;
}
bool NodeListManager::is_in_node_list(uint32_t macid) const {
    for (const auto& node : node_list) {
        if (node.macid == macid) {
            return true;
        }
    }
    return false;
}
