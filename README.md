# P2P Encrypted Chat System

This repository contains the initial version of a Peer-to-Peer (P2P) Encrypted Chat System implemented in **C**.

It supports **cross-platform TCP communication**, basic AES-256-CBC encryption, logging, and performance monitoring for message latency.

---

## **Features**

* Peer-to-peer connection over **TCP sockets**
* **AES-256-CBC encryption** for secure messaging
* Displays **own IP automatically** (client)
* Cross-platform support (**Windows**, **Linux**, **macOS**)
* Threaded architecture for **sending/receiving messages concurrently**
* Basic **performance/latency monitoring** with logs

> ⚠️ **Note**: This version is a prototype; encryption and performance monitoring are basic and intended for learning/testing.

---

## **Project Structure**

```
p2p-chat-system/
│
├── src/
│   └── p2pchat.c          # Main C source file containing all logic
├── encryption.h/c         # AES encryption utilities
├── utils.h/c              # Utility functions (logging, networking helpers)
├── Makefile               # Optional build script for Linux/macOS
├── README.md              # Project documentation
└── .gitignore             # Ignore build outputs
```

---

## **Prerequisites**

* **C Compiler**

  * Linux/macOS: `gcc`
  * Windows: `MinGW-w64`

  ```bash
  gcc --version
  ```

* **OpenSSL development library** (for encryption)

  ```bash
  # Linux
  sudo apt install libssl-dev

  # macOS
  brew install openssl
  ```

* **Windows**: Ensure `libcrypto` and `libssl` are linked when building

---

## **Building**

### Linux/macOS

```bash
cd src
gcc p2pchat.c encryption.c utils.c -o p2pchat -lcrypto -lssl -lpthread
```

### Windows (MinGW)

```bash
gcc p2pchat.c encryption.c utils.c -o p2pchat.exe -lws2_32 -lcrypto -lssl
```

---

## **Running the Chat**

1. **Start the server**

   ```bash
   ./p2pchat
   ```

   * Choose `server` mode
   * Enter the port to listen on

2. **Start the client**

   ```bash
   ./p2pchat
   ```

   * Choose `client` mode
   * The program auto-detects your IP and prompts for server port
   * It connects automatically to `127.0.0.1` (localhost)

3. **Commands in chat**

   * `stats` → display performance statistics
   * `reset` → reset statistics

---

## **Notes**

* Only **single peer-to-peer connections** are supported
* Encryption is **AES-256-CBC** with a hardcoded password (for demonstration only)
* Logs are saved to `../logs/chatlog.txt`
* Currently uses **localhost** only; LAN mode will be implemented in future versions

---
