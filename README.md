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
| `READ <filename>` | **READ** | Direct chunked read & display a file's contents |
| `STREAM <filename>` | **READ** | Word-by-word content streaming with 0.1s delay |
| `WRITE <filename> <sentence_num>` | **WRITE** | Edit a file at the word/sentence level |
| `DELETE <filename>` | **DELETE**| Cascading deletion & NM tracking eviction (owner only) |
| `ADDACCESS -R/-W <file> <user>` | **ACCESS**| Grant read or read/write access to a user |
| `REMACCESS <file> <user>` | **ACCESS**| Revoke access from a user |
| `INFO <filename>` | **INFO** | View file metadata and your current access level |
| `LIST` | **Feature 3** | Show all currently connected/registered users |
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

---

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

## Feature READ & STREAM (Direct Data Streaming)

- **Lookup & Status Authorization**: The Naming Server enforces user access control permissions and verifies that the Storage Server is actively online before returning its address to the client.
- **Direct Data Streaming**: The NM is kept out of the data path. The client establishes a direct TCP connection with the Storage Server. The SS streams the file in chunks using `FileChunkPacket` and terminates with a STOP packet (`chunk_size == 0`).
- **Word-by-word Simulation (`STREAM`)**: Displays contents word-by-word with a delay of 0.1 seconds between each word. Reports a clean error if the Storage Server goes down mid-stream.

### Expected Output (READ)
```
alice@docs++ > READ report.txt
  Looking up 'report.txt'...

─── report.txt ───────────────────────────────────
Hello from Docs++ NFS. This is a direct data stream test.
─────────────────────────────────────────────────
```

### Expected Output (Permission Denied)
```
bob@docs++ > READ report.txt
  Looking up 'report.txt'...
✗ ERROR: Permission denied for file 'report.txt'.
```

### Expected Output (SS Offline)
```
alice@docs++ > READ report.txt
  Looking up 'report.txt'...
✗ ERROR: Storage Server hosting 'report.txt' is offline.
```

### Expected Output (STREAM)
```
alice@docs++ > STREAM report.txt
  Looking up 'report.txt'...

─── STREAM: report.txt ─────────────────────────────
Hello from Docs++ NFS. This is a direct data stream test.
─────────────────────────────────────────────────
```

---

## Feature DELETE (System-Wide & Replication Eviction)

- **System-Wide Eviction**: Owners can delete their files. Deleting a file wipes the physical file from the active Storage Server and immediately evicts the entry from the Naming Server's lookup mapping so future client lookups instantly fail.
- **Replication Eviction**: The delete command automatically cascades to clear all replica storage server copies of the file.

### Expected Output (DELETE Success)
```
alice@docs++ > DELETE report.txt
  Deleting file 'report.txt'...
✓ File 'report.txt' deleted successfully.
```

### Expected Output (Non-owner Attempt)
```
bob@docs++ > DELETE report.txt
  Deleting file 'report.txt'...
✗ ERROR: Permission denied. Only the owner 'alice' can delete this file.
```

---

## Feature WRITE & ETIRW (Word-Level Editing)

- **Interactive Editing Session**: Users can update the content of a file at the word level. Initiating a write opens a direct connection to the hosting Storage Server.
- **In-Memory Word/Sentence Tokenization**: The Storage Server splits the file content into dynamic sentences and words using `.`, `!`, and `?` as sentence delimiters.
- **Dynamic Array Manipulation**: Insertions are handled using dynamic array shifts and splits. If an inserted word contains a delimiter, the sentence is split automatically.
- **Commit on ETIRW**: Typing `ETIRW` finishes the write session, saves the file back to disk on the Storage Server, recalculates word count, character count, and file size, and pushes these statistics to the Naming Server.

### Expected Output

```
alice@docs++ > WRITE report.txt 0
  Looking up 'report.txt'...
Client: 1 Hello from Docs++ NFS.
Client: ETIRW
✓ Write Successful!
```

---

## Feature 3 — LIST (Show Connected Users)

Displays all registered clients currently connected to the Naming Server.

### Expected Output
```
alice@docs++ > LIST

  Connected Users:
  Username              IP Address        Status  
  ───────────────────   ───────────────   ────────
  alice                 127.0.0.1         online  
  bob                   127.0.0.1         online  

  2 user(s) total.
```

---

## Feature 5 — Comprehensive Error Codes

Consolidates all error codes into a single, standardized set returned consistently across the system:

| Code Value | Macro Symbol | Scenario |
|:---:|---|---|
| `0` | `ERR_OK` | Success |
| `-1` | `ERR_NO_PERMISSION` | Unauthorized access |
| `-2` | `ERR_FILE_NOT_FOUND` | File not found |
| `-3` | `ERR_FILE_EXISTS` | File already exists |
| `-4` | `ERR_FILE_UNAVAILABLE` | Storage server is offline |
| `-5` | `ERR_NO_SS_AVAILABLE` | No Storage Server online |
| `-6` | `ERR_INTERNAL` | System / socket / file I/O failure |
| `-7` | `ERR_OUT_OF_RANGE` | Index out of range |
| `-8` | `ERR_MAX_CLIENTS` | Max registered client limit reached |
| `-9` | `ERR_MAX_FILES` | Naming Server registry full |
| `-10` | `ERR_FILE_LOCKED` | Resource contention / locking (future) |
| `-11` | `ERR_NETWORK` | Network failure |

---

## Feature — Access Control (ADDACCESS & REMACCESS)

- **Ownership-based Authorization**: The creator of a file automatically becomes its owner (with RW access). Only the owner can modify access permissions for other users.
- **Dynamic Updates**: Permissions are instantly synchronized with the Naming Server and take effect immediately for connected clients.

### Expected Output (Granting Access)
```
alice@docs++ > ADDACCESS -R report.txt bob
✓ Access granted successfully!
```

### Expected Output (Verifying Access)
```
bob@docs++ > INFO report.txt

─── report.txt ───────────────────────────────────
  Owner:          alice
  Size:           12 bytes
  Words:          2
  Characters:     11
  Created:        2026-07-05 22:52
  Last Modified:  2026-07-05 22:53
  Last Accessed:  2026-07-05 22:53
  Your Access:    R
─────────────────────────────────────────────────
```

### Expected Output (Revoking Access)
```
alice@docs++ > REMACCESS report.txt bob
✓ Access removed successfully!
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

---

## Change History (Feature Log)

### 2026-07-03
- **Feature 3 (LIST Connected Users)**: Implemented `LIST` command in client REPL, querying Naming Server for the active registry of client names, IP addresses, and online statuses.
- **Feature 5 (Universal Error Codes)**: Consolidated and standardized system-wide error definitions in `protocols.h`, mapping internal failure states to universal return codes.
- **Code Refactor & Bug Fixes**: Removed unused variables (e.g. `storage_dir` context propagation to threads), cleaned block comments, and fixed a bug where `handle_ss_delete` in storage server failed to send an ACK packet back to the naming server.

### 2026-07-05
- **Access Control (`ADDACCESS`, `REMACCESS`)**: Added the ability for file owners to dynamically grant Read (`-R`), Read/Write (`-W`), or completely revoke access for other users. Integrated Access Control Lists (ACL) into the Naming Server registry.
- **INFO Command Implementation**: Finished the missing implementation of `INFO` in the client CLI to display rich file metadata, including timestamps, sizes, and the current user's access level.
```
