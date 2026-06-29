CC=gcc
CFLAGS=-Wall -Wextra -O2 -pthread

COMMON=common/utils.c

all: nm_server ss_server nfs_client

nm_server: naming_server/main.c naming_server/storage_registry.c common/utils.c common/protocols.h common/utils.h naming_server/storage_registry.h
	$(CC) $(CFLAGS) -o $@ naming_server/main.c naming_server/storage_registry.c $(COMMON) -Icommon -Inaming_server

ss_server: storage_server/main.c storage_server/file_handler.c common/utils.c common/protocols.h common/utils.h storage_server/file_handler.h
	$(CC) $(CFLAGS) -o $@ storage_server/main.c storage_server/file_handler.c $(COMMON) -Icommon -Istorage_server

nfs_client: client/main.c common/utils.c common/protocols.h common/utils.h
	$(CC) $(CFLAGS) -o $@ client/main.c $(COMMON) -Icommon

clean:
	rm -f nm_server ss_server nfs_client