/*
 * storage_server/file_handler.h
 *
 * Preserved from Y-K1n9, extended with create_file helper.
 */

#ifndef FILE_HANDLER_H
#define FILE_HANDLER_H

#include "../common/protocols.h"

/* Scan <directory> for files; populate <packet> filenames and file_count.
 * Returns 0 on success, -1 on error. */
int scan_storage_directory(const char *directory,
                            SS_Registration_Packet *packet);

/* Read the entire content of <directory>/<filename> into a malloc'd buffer.
 * Caller must free(*buffer). Sets *length to byte count.
 * Returns 0 on success, -1 on error. */
int read_file_into_buffer(const char *directory, const char *filename,
                           char **buffer, long *length);

/* Create an empty file at <directory>/<filename>.
 * Returns 0 on success, -1 if it already exists or on I/O error. */
int create_empty_file(const char *directory, const char *filename);

/* Count words and characters in <directory>/<filename>.
 * Returns 0 on success. */
int count_file_stats(const char *directory, const char *filename,
                     int *words, int *chars, long *bytes);

#endif /* FILE_HANDLER_H */
