# Docs++ — Distributed Network File System

A distributed file system built with C, POSIX sockets, and pthreads.  
Implements a **Naming Server (NM) + Storage Server (SS) + Client** architecture.

> **This branch adds Feature 1 (CREATE) and Feature 4 (VIEW).**

---

## Quick Start (4 terminals)

### Step 0 — Build

```bash
make clean && make
```

Expected output:
```
gcc ... -o nm_server
gcc ... -o ss_server
gcc ... -o nfs_client
```

---

### Step 1 — Start the Naming Server (Terminal 1)

```bash
./nm_server
```

Expected output:
```
[NM] === Docs++ Name Server started on port 5000 ===
[NM Heartbeat] Listening for heartbeats on port 5001
```

---

### Step 2 — Start Storage Server 1 (Terminal 2)

```bash
mkdir -p ss_storage && echo "hello from ss1" > ss_storage/file1.txt
./ss_server 127.0.0.1 5000 6000 ./ss_storage
```

Expected output:
```
[SS] Registered 1 files with NM at 127.0.0.1:5000
[SS] Listening for direct operations on port 6000
```

---

### Step 3 — Start Storage Server 2 (Terminal 3)

```bash
mkdir -p ss_storage2 && echo "hello from ss2" > ss_storage2/file2.txt
./ss_server 127.0.0.1 5000 6001 ./ss_storage2
```

---

### Step 4 — Run the Client (Terminal 4)

```bash
./nfs_client alice
```

---

## Client Commands

| Command | Feature | Description |
|---|---|---|
| `CREATE <filename>` | **Feature 1** | Create a new empty file on the NFS |
| `VIEW` | **Feature 4** | List files you have access to |
| `VIEW -a` | **Feature 4** | List **all** files on the system |
| `VIEW -l` | **Feature 4** | Detailed listing (words, chars, timestamps) |
| `VIEW -al` | **Feature 4** | All files with full details |
| `READ <filename>` | Existing | Read and display a file's contents |
| `help` | — | Show command reference |
| `exit` | — | Disconnect |

---

## Feature 1 — CREATE

```
alice@docs++ > CREATE report.txt
✓ File 'report.txt' created successfully!
```

Flow:
```
Client ──CREATE──▶ NM ──SS_CREATE──▶ SS (creates empty file on disk)
                    ◀──────ACK──────
       ◀───response──
```

## Feature 4 — VIEW

```
alice@docs++ > VIEW -l

  Your files:
  Filename                      Owner             Words     Chars     Bytes  Modified           Accessed
  ─────────────────────────     ───────────────  ───────   ───────  ───────  ─────────────────  ─────────────────
  report.txt                    alice                  0         0         0  2024-11-05 22:31   2024-11-05 22:31
  file1.txt                     unknown                3        14        14  2024-11-05 22:28   2024-11-05 22:28
```

---

## Architecture

```
┌─────────────────────────────────────────────┐
│              Name Server (NM)               │
│  Port 5000 (main) | Port 5001 (heartbeat)  │
│                                             │
│  ┌──────────────┐  ┌─────────────────────┐ │
│  │  storage     │  │  file metadata      │ │
│  │  registry   │  │  (nm_files.json)   │ │
│  └──────────────┘  └─────────────────────┘ │
└────────────┬────────────────────────────────┘
             │ TCP
   ┌──────────┴──────────┐
   │                     │
┌──▼────────────┐  ┌──────▼──────────┐
│  SS 1         │  │  SS 2           │
│  Port 6000   │  │  Port 6001      │
│  ./ss_storage│  │  ./ss_storage2  │
└───────────────┘  └─────────────────┘
```

---

## Wire Protocol

All communication is **binary fixed-size structs** over TCP:

1. Sender writes `int32_t command_type` (the first field of every packet)
2. NM reads `command_type` first, then reads the rest of the struct

Command IDs defined in `common/protocols.h`.

---

## File Layout

```
.
├── common/
│   ├── protocols.h      ← all packet structs and command IDs
│   ├── utils.h
│   └── utils.c
├── naming_server/
│   ├── main.c           ← NM dispatcher + handlers
│   ├── storage_registry.h
│   └── storage_registry.c
├── storage_server/
│   ├── main.c           ← SS listener + heartbeat
│   ├── file_handler.h
│   └── file_handler.c
├── client/
│   └── main.c           ← CLI REPL
└── Makefile
```
