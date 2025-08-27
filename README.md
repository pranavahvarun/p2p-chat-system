# P2P Encrypted Chat System (Day 1 – C Implementation)

This repository contains the **Day-1 version** of a Peer-to-Peer (P2P) Encrypted Chat System implemented in **C**.  

### Features (Day 1)
- Direct peer-to-peer connection over TCP sockets  
- Basic XOR-based message encryption (demo only — not secure for production)  
- Cross-platform support (tested on Windows using MinGW-w64)  
- Lightweight, single-file implementation  

---

## **Project Structure**
p2p-chat-system/
├── src/
│ ├── main.c # Main C source file
│ ├── Makefile # For building using GNU Make
├── README.md
└── .gitignore

---

## **Prerequisites**

- **GCC (MinGW-w64)** for compiling C code  
  Check installation:
  ```bash
  gcc --version
