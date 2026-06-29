#ifndef FILE_HANDLER_H
#define FILE_HANDLER_H

#include "../common/protocols.h"

int scan_storage_directory(const char *directory, SS_Registration_Packet *packet);
int read_file_into_buffer(const char *directory, const char *filename, char **buffer, long *length);

#endif