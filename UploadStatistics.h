#ifndef UPLOAD_STATISTICS_H
#define UPLOAD_STATISTICS_H

class UploadStatistics
{
public:
    UploadStatistics();
    ~UploadStatistics();
    
    // Event tracking
    void on_packet_received();
    void on_checksum_error();
    void on_segments_requested(int count);
    
    // Getters
    int get_total_packets_received() const { return total_packets_received; }
    int get_total_packets_requested() const { return total_packets_requested; }
    int get_checksum_errors() const { return checksum_errors; }
    
    double get_link_rate_percent() const {
        return (total_packets_requested > 0) ? 
            (100.0 * total_packets_received / total_packets_requested) : 0.0;
    }
    
    // Reset for new upload
    void reset();
    
private:
    int total_packets_received;
    int total_packets_requested;
    int checksum_errors;
};

#endif // UPLOAD_STATISTICS_H
