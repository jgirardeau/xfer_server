#ifndef NODELIST_MANAGER_H
#define NODELIST_MANAGER_H

#include <string>
#include <vector>
#include <cstdint>
#include <chrono>

// Node information
struct NodeInfo {
    uint32_t macid;
    bool has_data_ready;
    
    NodeInfo(uint32_t id) : macid(id), has_data_ready(false) {}
};

class NodeListManager {
public:
    NodeListManager();
    ~NodeListManager();
    
    // Initialization
    void set_node_list_file(const std::string& filename);
    
    // Node list loading
    bool load_node_list();
    bool reload_node_list();
    
    // Node iteration
    bool has_nodes() const { return !node_list.empty(); }
    size_t get_node_count() const { return node_list.size(); }
    bool has_current_node() const;
    uint32_t get_current_macid() const;
    NodeInfo* get_current_node();
    size_t get_current_index() const { return current_node_index; }
    
    // Navigation
    void move_to_next_node();
    void reset_to_first_node();
    bool is_at_end() const;
    
    // Automatic reload management
    bool should_attempt_load();  // Returns true if enough time has passed
    bool check_and_reload_if_at_end();  // Auto-reloads if at end of list
    
    // Node access
    const std::vector<NodeInfo>& get_node_list() const { return node_list; }
    NodeInfo* find_node_by_macid(uint32_t macid);
    bool is_in_node_list(uint32_t macid) const;  // Check if MAC ID is in current node list
    
private:
    std::vector<NodeInfo> node_list;
    size_t current_node_index;
    std::string nodelist_filename;
    std::chrono::steady_clock::time_point last_load_attempt;
    
    static const int LOAD_RETRY_INTERVAL_SECONDS = 10;
};

#endif // NODELIST_MANAGER_H