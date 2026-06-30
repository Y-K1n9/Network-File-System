CC      = gcc
CFLAGS  = -Wall -Wextra -O2 -pthread

# ─── Source groups ───────────────────────────────────────────────────────────
COMMON_SRC = common/utils.c

NM_SRC  = naming_server/main.c \
           naming_server/storage_registry.c \
           $(COMMON_SRC)

NM_HDR  = common/protocols.h common/utils.h \
           naming_server/storage_registry.h

SS_SRC  = storage_server/main.c \
           storage_server/file_handler.c \
           $(COMMON_SRC)

SS_HDR  = common/protocols.h common/utils.h \
           storage_server/file_handler.h

CLIENT_SRC = client/main.c \
              $(COMMON_SRC)

CLIENT_HDR = common/protocols.h common/utils.h

# ─── Build targets ───────────────────────────────────────────────────────────
all: nm_server ss_server nfs_client

nm_server: $(NM_SRC) $(NM_HDR)
	$(CC) $(CFLAGS) -o nm_server $(NM_SRC)

ss_server: $(SS_SRC) $(SS_HDR)
	$(CC) $(CFLAGS) -o ss_server $(SS_SRC)

nfs_client: $(CLIENT_SRC) $(CLIENT_HDR)
	$(CC) $(CFLAGS) -o nfs_client $(CLIENT_SRC)

# ─── Helpers ─────────────────────────────────────────────────────────────────
clean:
	rm -f nm_server ss_server nfs_client nm_files.json

# Create default storage directories (run once before first boot)
setup:
	mkdir -p ss_storage ss_storage2
	echo "hello from ss1" > ss_storage/file1.txt
	echo "hello from ss2" > ss_storage2/file2.txt

.PHONY: all clean setup
