#include "UploadStatistics.h"

UploadStatistics::UploadStatistics()
    : total_packets_received(0),
      total_packets_requested(0),
      checksum_errors(0)
{
}

UploadStatistics::~UploadStatistics()
{
}

void UploadStatistics::on_packet_received()
{
    total_packets_received++;
}

void UploadStatistics::on_checksum_error()
{
    checksum_errors++;
}

void UploadStatistics::on_segments_requested(int count)
{
    total_packets_requested += count;
}

void UploadStatistics::reset()
{
    total_packets_received = 0;
    total_packets_requested = 0;
    checksum_errors = 0;
}
