# P2P Encrypted Chat System (C Version)

This repository contains a **cross-platform P2P Encrypted Chat System** implemented in **C**.

### Features

* Direct peer-to-peer connection over **TCP sockets**
* AES-256-CBC encryption for secure messaging
* Cross-platform support (Windows using MinGW, Linux, macOS)
* LAN IP detection for easy connection
* Latency measurement and performance monitoring
* Thread-safe logging of chat messages

---

## **Project Structure**
```
p2p-chat-system/
│
├── src/
│    ├── p2pchat.c # Main program: server/client flow, threads, and I/O
│    ├── encryption.c # AES encryption/decryption functions
│    ├── encryption.h # Header for encryption functions
│    ├── utils.c # Utility functions (logging, performance tracking, parsing)
│    └── utils.h # Header for utility functions
│
├── logs/ # Automatically created at runtime
│    └── chatlog.txt # Thread-safe log of all messages
│
├── README.md # Project documentation (this file)
└── .gitignore # Ignore build outputs and logs
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

## **Commands During Chat**

* `stats` — Show current latency/performance statistics
* `reset` — Reset performance statistics

---

## **Logging**

* Chat messages are logged in `../logs/chatlog.txt`
* Logging is **thread-safe** and includes timestamps.

---

## **Notes**

* Ensure both peers are on the **same LAN** for easy connection.
* This version removes the need to hardcode IPs — LAN IP detection is automatic.
* For testing on the same machine, you can use `127.0.0.1` as server IP.

---

## **License**

This project is MIT licensed. See `LICENSE` file for details.

