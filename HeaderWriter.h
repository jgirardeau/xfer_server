#ifndef HEADER_WRITER_H
#define HEADER_WRITER_H

#include <string>
#include "CommandProcessor.h"

/**
 * Write a header log entry for a completed upload
 * 
 * @param triggering_response The response that triggered the upload (contains all metadata)
 * @param data_size Number of samples received
 */
void write_header_log_entry(const CommandResponse* triggering_response, size_t data_size);

#endif // HEADER_WRITER_H
