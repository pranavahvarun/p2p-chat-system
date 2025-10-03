# P2P Encrypted Chat System (C Version)

This repository contains a **cross-platform P2P Encrypted Chat System** implemented in **C**.

### Features

* Direct peer-to-peer connection over **TCP sockets**
* AES-256-CBC encryption for secure messaging
* Cross-platform support (Windows using MinGW, Linux, macOS)
* LAN IP detection for easy connection
* Latency measurement and performance monitoring
* Thread-safe logging of chat messages
* **Message history** saved to a file (`../logs/chat_history.txt`)
* **File transfer** support using `/sendfile <filename>`

---

## **Project Structure**

```
p2p-chat-system/
│
├── src/
│    ├── p2pchat.c       # Main program: server/client flow, threads, and I/O
│    ├── encryption.c    # AES encryption/decryption functions
│    ├── encryption.h    # Header for encryption functions
│    ├── udp_chat.c # UDP chat implementation
│    ├── udp_chat.h # Header for UDP chat
│    ├── utils.c         # Utility functions (logging, performance tracking, parsing)
│    └── utils.h         # Header for utility functions
│
├── downloads/           # Automatically created when receiving files
│    └── <received files> # All incoming files saved here
│
├── logs/                # Automatically created at runtime
│    ├── chatlog.txt      # Thread-safe log of all messages
│    └── chat_history.txt # Saved message history
│
├── README.md            # Project documentation (this file)
└── .gitignore           # Ignore build outputs and logs
```

---

## **Prerequisites**

* **GCC (MinGW-w64)** for Windows or Linux/macOS
  Check installation:

```bash
gcc --version
```

* **OpenSSL library** (for AES encryption)
  Install on Linux/macOS:

```bash
sudo apt install libssl-dev   # Debian/Ubuntu
brew install openssl          # macOS
```

---

## **Build Instructions**

### Linux/macOS

```bash
cd src
gcc -pthread p2pchat.c encryption.c utils.c -o p2pchat -lcrypto -lssl
```

### Windows (MinGW)

```bash
cd src
gcc p2pchat.c encryption.c utils.c -o p2pchat.exe -lws2_32 -lcrypto -lssl
```

---

## **Usage**

1. Run the program:

```bash
./p2pchat        # Linux/macOS
p2pchat.exe      # Windows
```

2. Choose mode: `server` or `client`.

### **Server**

* Enter port to listen on.
* Your LAN IP will be displayed automatically.
* Wait for a peer to connect.

### **Client**

* Your own IP will be displayed automatically.
* Enter the **server’s LAN IP** and port to connect.

---

## **Chat Commands**

* `stats` — Show current latency/performance statistics
* `reset` — Reset performance statistics
* `/history` — Display saved chat history
* `/sendfile <filename>` — Send a file to the connected peer

  * All received files are automatically saved under `../downloads/`

---

## **Logging & History**

* **Thread-safe logging** of all messages is in `../logs/chatlog.txt`.
* **Message history** is saved in `../logs/chat_history.txt` and can be viewed during chat with `/history`.
* Received files are stored in `../downloads/` automatically.

---

## **Notes**

* Ensure both peers are on the **same LAN** for easy connection.
* This version removes the need to hardcode IPs — LAN IP detection is automatic.
* For testing on the same machine, you can use `127.0.0.1` as server IP.
* File transfer works for **any file type**. Large files are sent in encrypted chunks.

---

## **License**

This project is MIT licensed. See `LICENSE` file for details.
