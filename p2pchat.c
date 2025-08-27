/*
 * p2pchat.c — Cross-platform P2P chat (Day 1 parity with your Python script)
 *
 * Save as: p2pchat.c
 * Build (Linux/macOS): gcc -pthread p2pchat.c -o p2pchat
 * Build (Windows MinGW): gcc p2pchat.c -o p2pchat.exe -lws2_32
 *
 * Logs -> ../logs/chatlog.txt  (relative to src/)
 *
 * Features:
 *  - server/client selection
 *  - TCP sockets
 *  - concurrent send/receive
 *  - timestamps [HH:MM:SS]
 *  - thread-safe logging to ../logs/chatlog.txt
 *  - IP/port validation
 *  - graceful shutdown on Ctrl+C and on peer disconnect
 */

#define _CRT_SECURE_NO_WARNINGS

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include "encryption.h"
const char *SECRET_KEY = "admin123";



#ifdef _WIN32
  /* Windows */
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #include <windows.h>
  #include <direct.h> /* _mkdir */
  typedef SOCKET sock_t;
  #define sock_invalid INVALID_SOCKET
  #define close_socket(s) closesocket(s)
  #define SHUT_RDWR_SD SD_BOTH
  #define mkdir_path(p) _mkdir(p)
#else
  /* POSIX */
  #include <unistd.h>
  #include <errno.h>
  #include <signal.h>
  #include <arpa/inet.h>
  #include <sys/types.h>
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <sys/stat.h>
  #include <pthread.h>
  typedef int sock_t;
  #define sock_invalid -1
  #define close_socket(s) close(s)
  #define SHUT_RDWR_SD SHUT_RDWR
  #define mkdir_path(p) mkdir((p), 0755)
#endif

/* Common */
#define RECV_BUF 4096
#define SEND_BUF 4096
#define LOG_DIR "../logs"
#define LOG_FILE "../logs/chatlog.txt"

static volatile int running = 1;
static sock_t conn_sock = sock_invalid;

/* Cross-platform thread & mutex types */
#ifdef _WIN32
  typedef HANDLE thread_t;
  typedef HANDLE mutex_t;
#else
  typedef pthread_t thread_t;
  typedef pthread_mutex_t mutex_t;
#endif

static mutex_t log_mutex;

/* ---------- cross-platform primitives ---------- */

static void mutex_init(mutex_t *m) {
#ifdef _WIN32
    *m = CreateMutex(NULL, FALSE, NULL);
#else
    pthread_mutex_init(m, NULL);
#endif
}
static void mutex_lock(mutex_t *m) {
#ifdef _WIN32
    WaitForSingleObject(*m, INFINITE);
#else
    pthread_mutex_lock(m);
#endif
}
static void mutex_unlock(mutex_t *m) {
#ifdef _WIN32
    ReleaseMutex(*m);
#else
    pthread_mutex_unlock(m);
#endif
}
static void mutex_destroy(mutex_t *m) {
#ifdef _WIN32
    CloseHandle(*m);
#else
    pthread_mutex_destroy(m);
#endif
}

/* thread start */
#ifdef _WIN32
  static thread_t start_thread(LPTHREAD_START_ROUTINE fn, void *arg) {
      return CreateThread(NULL, 0, fn, arg, 0, NULL);
  }
#else
  static thread_t start_thread(void *(*fn)(void*), void *arg) {
      pthread_t th;
      pthread_create(&th, NULL, fn, arg);
      return th;
  }
#endif

/* thread join */
static void join_thread(thread_t t) {
#ifdef _WIN32
    WaitForSingleObject(t, INFINITE);
    CloseHandle(t);
#else
    pthread_join(t, NULL);
#endif
}

/* ---------- utilities ---------- */

static void trim_newline(char *s) {
    if (!s) return;
    size_t n = strlen(s);
    while (n && (s[n-1] == '\n' || s[n-1] == '\r')) s[--n] = '\0';
}

static void timestamp_now(char *out, size_t out_sz) {
    time_t t = time(NULL);
#ifdef _WIN32
    struct tm tm_;
    localtime_s(&tm_, &t);
    strftime(out, out_sz, "[%H:%M:%S]", &tm_);
#else
    struct tm tm_;
    localtime_r(&t, &tm_);
    strftime(out, out_sz, "[%H:%M:%S]", &tm_);
#endif
}

/* ensure logs dir exists */
static void ensure_logs_dir(void) {
    /* attempt create - ignore errors if exists */
    mkdir_path(LOG_DIR);
}

/* thread-safe logging */
static void log_message(const char *fmt, ...) {
    ensure_logs_dir();
    mutex_lock(&log_mutex);
    FILE *f = fopen(LOG_FILE, "a");
    if (!f) {
        /* if log open fails still unlock and print to stderr */
        perror("[ERROR] fopen log");
        mutex_unlock(&log_mutex);
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    fprintf(f, "\n");
    va_end(ap);
    fclose(f);
    mutex_unlock(&log_mutex);
}

/* portable sleep ms */
static void sleep_ms(int ms) {
#ifdef _WIN32
    Sleep(ms);
#else
    struct timespec ts = { ms / 1000, (ms % 1000) * 1000000 };
    nanosleep(&ts, NULL);
#endif
}

/* ---------- input validation ---------- */

static int parse_port(const char *s, int *out_port) {
    if (!s || !*s) return 0;
    char *end = NULL;
    long v = strtol(s, &end, 10);
    if (*end != '\0') return 0;
    if (v < 1 || v > 65535) return 0;
    *out_port = (int)v;
    return 1;
}

static int validate_ip(const char *ip) {
    if (!ip || !*ip) return 0;
    struct in_addr addr;
    return inet_pton(AF_INET, ip, &addr) == 1;
}

/* ---------- networking helpers ---------- */

static sock_t start_server(int port) {
    sock_t s;
#ifdef _WIN32
    s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) {
        fprintf(stderr, "[ERROR] socket: %d\n", WSAGetLastError());
        return sock_invalid;
    }
#else
    s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) {
        perror("[ERROR] socket");
        return sock_invalid;
    }
#endif

    int opt = 1;
    setsockopt((int)s, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_ANY);
    sa.sin_port = htons((unsigned short)port);

    if (bind((int)s, (struct sockaddr*)&sa, sizeof(sa)) != 0) {
#ifdef _WIN32
        fprintf(stderr, "[ERROR] bind: %d\n", WSAGetLastError());
        closesocket(s);
#else
        perror("[ERROR] bind");
        close(s);
#endif
        return sock_invalid;
    }

    if (listen((int)s, 1) != 0) {
#ifdef _WIN32
        fprintf(stderr, "[ERROR] listen: %d\n", WSAGetLastError());
        closesocket(s);
#else
        perror("[ERROR] listen");
        close(s);
#endif
        return sock_invalid;
    }

    printf("[INFO] Waiting for peer to connect on port %d...\n", port);
    fflush(stdout);

    struct sockaddr_in cli;
    socklen_t len = sizeof(cli);
    int c = (int)accept((int)s, (struct sockaddr*)&cli, &len);
    if (c < 0) {
#ifdef _WIN32
        fprintf(stderr, "[ERROR] accept: %d\n", WSAGetLastError());
        closesocket(s);
#else
        perror("[ERROR] accept");
        close(s);
#endif
        return sock_invalid;
    }

    char ipstr[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &cli.sin_addr, ipstr, sizeof(ipstr));
    printf("[CONNECTED] Peer connected from %s:%d\n", ipstr, ntohs(cli.sin_port));
#ifdef _WIN32
    closesocket(s); /* close listening socket */
#else
    close(s);
#endif
    return (sock_t)c;
}

static sock_t start_client(const char *peer_ip, int peer_port) {
    sock_t s;
#ifdef _WIN32
    s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) {
        fprintf(stderr, "[ERROR] socket: %d\n", WSAGetLastError());
        return sock_invalid;
    }
#else
    s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) {
        perror("[ERROR] socket");
        return sock_invalid;
    }
#endif

    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons((unsigned short)peer_port);

    if (inet_pton(AF_INET, peer_ip, &sa.sin_addr) != 1) {
        fprintf(stderr, "[ERROR] Invalid IP address format.\n");
#ifdef _WIN32
        closesocket(s);
#else
        close(s);
#endif
        return sock_invalid;
    }

    if (connect((int)s, (struct sockaddr*)&sa, sizeof(sa)) != 0) {
#ifdef _WIN32
        fprintf(stderr, "[ERROR] connect: %d\n", WSAGetLastError());
        closesocket(s);
#else
        perror("[ERROR] connect");
        close(s);
#endif
        return sock_invalid;
    }

    printf("[CONNECTED] Connected to peer at %s:%d\n", peer_ip, peer_port);
    return s;
}

/* ---------- receiver thread ---------- */

#ifdef _WIN32
  DWORD WINAPI receiver_fn(LPVOID arg)
#else
  void *receiver_fn(void *arg)
#endif
{
    (void)arg;
    char buf[RECV_BUF];

    while (running) {
#ifdef _WIN32
        int n = recv(conn_sock, buf, (int)sizeof(buf) - 1, 0);
#else
        ssize_t n = recv(conn_sock, buf, sizeof(buf) - 1, 0);
#endif
        if (n == 0) {
            printf("\n[INFO] Connection closed by peer.\n");
            log_message("[%s] Peer disconnected.", /* dummy TS handled below */ "");
            running = 0;
            break;
        } else if (n < 0) {
#ifdef _WIN32
            int err = WSAGetLastError();
            if (err == WSAEINTR) continue;
            fprintf(stderr, "\n[ERROR] recv: %d\n", err);
#else
            if (errno == EINTR) continue;
            perror("\n[ERROR] recv");
#endif
            running = 0;
            break;
        }
        unsigned char decrypted[RECV_BUF];
        int dec_len = decrypt_message((unsigned char*)buf, n, (unsigned char*)SECRET_KEY, decrypted, RECV_BUF);
        if (dec_len < 0) {
            fprintf(stderr, "[ERROR] Failed to decrypt message.\n");
            continue;
        }

        // Null-terminate for printing
        decrypted[dec_len] = '\0';
        char ts[16];
        timestamp_now(ts, sizeof(ts));
        printf("\n%s Peer: %s\n", ts, decrypted);
        log_message("%s Peer: %s", ts, decrypted);
        printf("You: ");
        fflush(stdout);
    }
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

/* ---------- shutdown handling ---------- */

#ifdef _WIN32
static BOOL WINAPI console_handler(DWORD signal) {
    if (signal == CTRL_C_EVENT) {
        running = 0;
        if (conn_sock != sock_invalid) closesocket(conn_sock);
        printf("\n[INFO] Shutting down...\n");
    }
    return TRUE;
}
#else
static void sigint_handler(int signum) {
    (void)signum;
    running = 0;
    if (conn_sock != sock_invalid) close(conn_sock);
    printf("\n[INFO] Shutting down...\n");
}
#endif

/* ---------- main flow (mimic Python) ---------- */

int main(void) {
    /* Winsock init if needed */
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) {
        fprintf(stderr, "[ERROR] WSAStartup failed.\n");
        return 1;
    }
#endif

    mutex_init(&log_mutex);

    /* Console signal handler */
#ifdef _WIN32
    SetConsoleCtrlHandler(console_handler, TRUE);
#else
    struct sigaction sa;
    sa.sa_handler = sigint_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
#endif

    printf("=== P2P Chat System ===\n");
    printf("Start as (server/client)? ");
    fflush(stdout);

    char mode[32];
    if (!fgets(mode, sizeof(mode), stdin)) goto cleanup;
    trim_newline(mode);

    if (strcmp(mode, "server") != 0 && strcmp(mode, "client") != 0) {
        fprintf(stderr, "[ERROR] Invalid mode. Please choose 'server' or 'client'.\n");
        goto cleanup;
    }

    if (strcmp(mode, "server") == 0) {
        printf("Enter port to listen on: ");
        fflush(stdout);
        char port_s[32];
        if (!fgets(port_s, sizeof(port_s), stdin)) goto cleanup;
        trim_newline(port_s);
        int port = 0;
        if (!parse_port(port_s, &port)) {
            fprintf(stderr, "[ERROR] Invalid port. Must be between 1 and 65535.\n");
            goto cleanup;
        }
        conn_sock = start_server(port);
        if (conn_sock == sock_invalid) goto cleanup;

    } else {
        char ip[64], port_s[32];
        printf("Enter peer IP address: ");
        fflush(stdout);
        if (!fgets(ip, sizeof(ip), stdin)) goto cleanup;
        trim_newline(ip);
        if (!validate_ip(ip)) {
            fprintf(stderr, "[ERROR] Invalid IP address format.\n");
            goto cleanup;
        }
        printf("Enter peer port: ");
        fflush(stdout);
        if (!fgets(port_s, sizeof(port_s), stdin)) goto cleanup;
        trim_newline(port_s);
        int port = 0;
        if (!parse_port(port_s, &port)) {
            fprintf(stderr, "[ERROR] Invalid port. Must be between 1 and 65535.\n");
            goto cleanup;
        }
        conn_sock = start_client(ip, port);
        if (conn_sock == sock_invalid) goto cleanup;
    }

    /* start receiver thread */
#ifdef _WIN32
    thread_t rx = start_thread(receiver_fn, NULL);
    if (!rx) {
        fprintf(stderr, "[ERROR] CreateThread failed.\n");
        goto cleanup;
    }
#else
    thread_t rx = start_thread(receiver_fn, NULL);
    /* pthread_create returns non-zero on error; we ignore check here because start_thread wraps it */
#endif

    /* sender loop */
    char line[SEND_BUF];
    while (running) {
        printf("You: ");
        fflush(stdout);

        if (!fgets(line, sizeof(line), stdin)) {
            /* EOF */
            running = 0;
            break;
        }

        trim_newline(line);

        if (line[0] == '\0') {
            printf("[WARN] Cannot send empty message.\n");
            continue;
        }

        // Encrypt before sending
        unsigned char encrypted[SEND_BUF];
        int enc_len = encrypt_message((unsigned char*)line, strlen(line), 
                                    (unsigned char*)SECRET_KEY, encrypted, SEND_BUF);
        if (enc_len < 0) {
            fprintf(stderr, "[ERROR] Failed to encrypt message.\n");
            continue;
        }

        printf("Encrypted bytes: ");
        for (int i = 0; i < enc_len; i++)
            printf("%02X ", encrypted[i]);
        printf("\n");

        #ifdef _WIN32
            int sent = send(conn_sock, (const char*)encrypted, enc_len, 0);
            if (sent == SOCKET_ERROR) {
                fprintf(stderr, "\n[ERROR] send: %d\n", WSAGetLastError());
                running = 0;
                break;
            }
        #else
            ssize_t sent = send(conn_sock, (const char*)encrypted, enc_len, 0);
            if (sent < 0) {
                perror("\n[ERROR] send");
                running = 0;
                break;
            }
        #endif
        char ts[16];
        timestamp_now(ts, sizeof(ts));
        log_message("%s You: %s", ts, line);
    }


    /* wait for receiver thread */
    join_thread(rx);

cleanup:
    if (conn_sock != sock_invalid) {
#ifdef _WIN32
        shutdown(conn_sock, SD_BOTH);
        closesocket(conn_sock);
#else
        shutdown(conn_sock, SHUT_RDWR);
        close(conn_sock);
#endif
    }

#ifdef _WIN32
    WSACleanup();
#endif
    mutex_destroy(&log_mutex);

    return 0;
}
