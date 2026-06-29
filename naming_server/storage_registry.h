#ifndef STORAGE_REGISTRY_H
#define STORAGE_REGISTRY_H

#include "../common/protocols.h"
#include <stddef.h>

typedef struct {
    SS_Registration_Packet packet;
    int in_use;
} RegistryEntry;

void registry_init(void);
void registry_upsert(const SS_Registration_Packet *packet);
int registry_find_file(const char *filename, SS_Registration_Packet *out);
void registry_list_files(char *buffer, size_t buffer_size);

#endif