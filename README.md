To run the implementation, you will need **4 terminal windows** (or tabs).


### Step 0: Clone the Repository
In the main terminal, clone the project, then navigate into the project root directory:
```bash
git clone https://github.com/Y-K1n9/Network-File-System
```
---

### Step 1: Compilation
In any terminal, compile the codebase:
```bash
make clean && make
```
* **Expected Output:** Compiles `nm_server`, `ss_server`, and `nfs_client` successfully.

---

### Step 2: Boot Naming Server (Terminal 1)
```bash
./nm_server
```
* **Expected Output:**
  ```text
  [NM] Listening on port 5000
  [NM Heartbeat] Listening for heartbeats on port 5001
  ```
  *(The Naming Server boots up both the primary listener and the background heartbeat timeout tracker).*

---

### Step 3: Boot Storage Server 1 (Terminal 2)
Boot up Storage Server 1 (pointing to port 6000 and the `./ss_storage` folder containing `file1.txt`):
```bash
./ss_server 127.0.0.1 5000 6000 ./ss_storage
```
* **Expected Output in Terminal 2:**
  ```text
  [SS] Registered 1 files with NM at 127.0.0.1:5000
  [SS] Listening for direct operations on port 6000
  ```
* **Expected Output in Terminal 1 (Naming Server):**
  ```text
  [NM] Received registration from SS at 127.0.0.1.
  [NM] SS client port: 6000, files: 1
  ```
  *(The storage server spawns a background ping client transmitting heartbeats to port 5001 every 1.5 seconds).*

---

### Step 4: Boot Storage Server 2 (Terminal 3)
Set up a replica directory and boot up a second Storage Server on port 6001:
```bash
mkdir -p ss_storage2
echo "hello from storage server 2 - replica" > ss_storage2/file1.txt
./ss_server 127.0.0.1 5000 6001 ./ss_storage2
```
* **Expected Output in Terminal 3:**
  ```text
  [SS] Registered 1 files with NM at 127.0.0.1:5000
  [SS] Listening for direct operations on port 6001
  ```
* **Expected Output in Terminal 1 (Naming Server):**
  ```text
  [NM] Received registration from SS at 127.0.0.1.
  [NM] SS client port: 6001, files: 1
  ```

---

### Step 5: Test Client Handshake & Lookup (Terminal 4)
Run the client binary:
```bash
./nfs_client
```
* **Inputs:**
  * Username: `testuser`
  * File to read: `file1.txt`
* **Expected Output in Terminal 4 (Client):**
  ```text
  Username: testuser
  [Client] Handshake Success: Welcome testuser! Registration successful.
  File to read: file1.txt
  [Client] NM response: File 'file1.txt' found on 127.0.0.1:6000
  hello from storage server
  this is a sample file
  ```
* **Expected Output in Terminal 1 (Naming Server):**
  ```text
  [NM] Client 'testuser' registered from 127.0.0.1
  [NM] Lookup for 'file1.txt' by testuser resolved to 127.0.0.1:6000
  ```
  > [!NOTE]
  > This demonstrates **Client Handshake Validation** (Step 1 of client lookup will fail/drop if not registered) and **Primary File Routing**.

---

### Step 6: Heartbeat Timeout & Automatic Failover (Terminal 4)
Now, demonstrate dynamic timeout tracking and failover fallback:
1. **Kill Storage Server 1**: Go to **Terminal 2** (Storage Server 1) and press `Ctrl+C`.
2. **Observe Naming Server (Terminal 1)**: Wait 5 seconds.
   * **Expected Output in Terminal 1:**
     ```text
     [NM] Storage Server on port 6000 timed out (marked OFFLINE)
     ```
3. **Perform Client Lookup again (Terminal 4)**: Run `./nfs_client` again.
   * **Inputs:**
     * Username: `testuser`
     * File to read: `file1.txt`
   * **Expected Output in Terminal 4:**
     ```text
     Username: testuser
     [Client] Handshake Success: Welcome testuser! Registration successful.
     File to read: file1.txt
     [Client] NM response: File 'file1.txt' found on 127.0.0.1:6001
     hello from storage server 2 - replica
     ```
   * **Expected Output in Terminal 1:**
     ```text
     [NM] Client 'testuser' registered from 127.0.0.1
     [NM] Lookup for 'file1.txt' by testuser resolved to 127.0.0.1:6001
     ```
  > 
  > [!NOTE] Our Naming Server dynamically marked port `6000` as offline, and instead of failing, the routing lookup automatically retrieved the second pointer (port `6001`) from the replica list, providing seamless failover.

---

### Step 7: Secure Path Verification (Terminal 4)
Test the directory traversal block:
1. Run `./nfs_client`.
2. **Inputs:**
   * Username: `testuser`
   * File to read: `../../etc/passwd`
3. **Expected Output in Terminal 4:**
   ```text
   [Client] NM response error: File '../../etc/passwd' not found
   ```
   *(The Naming Server database lookup blocks it. Direct file handlers on the Storage Server also contain secure path checks verifying that incoming filenames do not contain relative navigation segments like `../` to prevent traversals).*
