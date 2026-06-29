To run the implementation, you will need **3 terminal windows**  open on the system inside the my_nfs_project directory.

### Step 0: Clone the Repository
In the main terminal, clone the project, then navigate into the project root directory:
```bash
git clone https://github.com/Y-K1n9/Network-File-System
```
---
### Step 1: Compilation
In any terminal, compile the project using the `Makefile`:
```bash
make clean && make
```
* **Expected Output:**
  ```text
  rm -f nm_server ss_server nfs_client
  gcc -Wall -Wextra -O2 -pthread -o nm_server naming_server/main.c naming_server/storage_registry.c common/utils.c -Icommon -Inaming_server
  gcc -Wall -Wextra -O2 -pthread -o ss_server storage_server/main.c storage_server/file_handler.c common/utils.c -Icommon -Istorage_server
  gcc -Wall -Wextra -O2 -pthread -o nfs_client client/main.c common/utils.c -Icommon
  ```

---

### Step 2: Run the Naming Server (Terminal 1)
Start the central coordinator:
```bash
./nm_server
```
* **Expected Output:**
  ```text
  [NM] Listening on port 5000
  ```
  *(It will block here, waiting for connections).*

---

### Step 3: Run the Storage Server (Terminal 2)
In the second terminal, boot up the Storage Server. The SS scans `./ss_storage/`, finds `file1.txt`, registers it with the NM, and sits listening for direct client connections:
```bash
./ss_server
```
* **Expected Output in Terminal 2 (Storage Server):**
  ```text
  [SS] Registered 1 files with NM at 127.0.0.1:5000
  [SS] Listening for direct reads on port 6000
  ```
* **Expected Output in Terminal 1 (Naming Server):**
  ```text
  [NM] Received registration from SS at 127.0.0.1.
  [NM] SS client port: 6000, files: 1
  ```
  > [!NOTE]
  > This demonstrates **Feature 1 (Ping Registration)** and **Feature 2 (Directory Scanning)** successfully.

---

### Step 4: Run the Client (Terminal 3)
In the third terminal, start the client binary:
```bash
./nfs_client
```
The program will prompt you for a username and a filename. Enter:
1. **Username:** `viva_evaluator`
2. **File to read:** `file1.txt`

* **Expected Output in Terminal 3 (Client):**
  ```text
  Username: viva_evaluator
  File to read: file1.txt
  [Client] NM response: File 'file1.txt' found on 127.0.0.1:6000
  hello from storage server
  this is a sample file
  ```
* **Expected Output in Terminal 1 (Naming Server):**
  ```text
  [NM] Lookup for 'file1.txt' by viva_evaluator resolved to 127.0.0.1:6000
  ```
  > [!NOTE]
  > This demonstrates **Feature 3 (Client Handshake & Lookup)** and **Feature 4 (Direct File Read)** successfully.

---
