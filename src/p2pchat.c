/*
 * p2pchat.c — Cross-platform P2P chat with Encryption + Performance Monitor
 *
 * Save as: p2pchat.c
 * Build (Linux/macOS): gcc -pthread p2pchat.c encryption.c utils.c -o p2pchat -lcrypto -lssl
 * Build (Windows MinGW): gcc p2pchat.c encryption.c utils.c -o p2pchat.exe -lws2_32 -lcrypto -lssl
 *
 * Features:
 *  - Part 1: Core TCP socket communication (server/client)
 *  - Part 2: AES-256-CBC encryption with password
 *  - Part 3: Latency measurement and performance monitoring
 */

#define _CRT_SECURE_NO_WARNINGS

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include "encryption.h"
#include "utils.h"

const char *SECRET_KEY = "admin123";

#ifdef _WIN32
  /* Windows */
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #include <windows.h>
  #include <direct.h>
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
static unsigned char derived_key[ENC_KEY_LEN];

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
    mkdir_path(LOG_DIR);
}

/* thread-safe logging */
static void log_message(const char *fmt, ...) {
    ensure_logs_dir();
    mutex_lock(&log_mutex);
    FILE *f = fopen(LOG_FILE, "a");
    if (!f) {
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

/* Save chat history to a separate file */
static void save_history(const char *who, int seq, const char *msg) {
    ensure_logs_dir();
    mutex_lock(&log_mutex);
    FILE *f = fopen("../logs/chat_history.txt", "a");
    if (!f) {
        perror("[ERROR] fopen history");
        mutex_unlock(&log_mutex);
        return;
    }
    time_t now = time(NULL);
    char ts[32];
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", localtime(&now));
    fprintf(f, "[%s] %s (seq=%d): %s\n", ts, who, seq, msg);
    fclose(f);
    mutex_unlock(&log_mutex);
}

void view_chat_history() {
    FILE *fp = fopen("../logs/chat_history.txt", "r");
    if (!fp) {
        printf("No chat history found.\n");
        return;
    }

    char line[1024];
    printf("\n===== Chat History =====\n");
    while (fgets(line, sizeof(line), fp)) {
        printf("%s", line);
    }
    printf("========================\n\n");

    fclose(fp);
}

static void ensure_downloads_dir(void) {
    mkdir_path("../downloads");
}

void send_file(sock_t sock, const char *filepath, unsigned char *key) {
    FILE *f = fopen(filepath, "rb");
    if (!f) {
        perror("[ERROR] fopen");
        return;
    }

    // Get file size
    fseek(f, 0, SEEK_END);
    long filesize = ftell(f);
    fseek(f, 0, SEEK_SET);

    // Send a header first: "FILE:<filename>:<size>"
    char filename[256];
    const char *slash = strrchr(filepath, '/');
    if (!slash) slash = strrchr(filepath, '\\');
    if (slash) strcpy(filename, slash + 1);
    else strcpy(filename, filepath);

    char header[512];
    snprintf(header, sizeof(header), "FILE:%s:%ld", filename, filesize);
    unsigned char encrypted_header[SEND_BUF];
    int enc_len = encrypt_message((unsigned char*)header, strlen(header), key, encrypted_header, SEND_BUF);
    send(sock, (const char*)encrypted_header, enc_len, 0);

    // Send file in chunks
    unsigned char buf[1024];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        unsigned char enc_chunk[2048];
        int enc_chunk_len = encrypt_message(buf, n, key, enc_chunk, sizeof(enc_chunk));
        send(sock, (const char*)enc_chunk, enc_chunk_len, 0);
    }

    fclose(f);
    printf("[INFO] File '%s' sent successfully.\n", filename);
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

/* Get local LAN IP (IPv4) */
static void get_local_ip(char *buffer, size_t buflen) {
    char host[256];
    if (gethostname(host, sizeof(host)) == -1) {
        strncpy(buffer, "Unknown", buflen);
        return;
    }

    struct hostent *host_entry = gethostbyname(host);
    if (!host_entry) {
        strncpy(buffer, "Unknown", buflen);
        return;
    }

    const char *ip = inet_ntoa(*(struct in_addr*)host_entry->h_addr_list[0]);
    strncpy(buffer, ip, buflen);
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

    char local_ip[64];
    get_local_ip(local_ip, sizeof(local_ip));
    printf("[INFO] Server started.\n");
    printf("[INFO] Your LAN IP: %s\n", local_ip);
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
    closesocket(s);
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
            log_message("Peer disconnected.");
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

        /* Decrypt message */
        unsigned char decrypted[RECV_BUF];
        int dec_len = decrypt_message((unsigned char*)buf, n, derived_key, 
                                     decrypted, RECV_BUF);
        if (dec_len < 0) {
            fprintf(stderr, "[ERROR] Failed to decrypt message.\n");
            continue;
        }

        decrypted[dec_len] = '\0';
        
        /* Parse message for performance tracking */
        char clean_message[RECV_BUF];
        uint32_t sequence = 0;
        int is_tracked = perf_parse_message((char*)decrypted, clean_message, 
                                           sizeof(clean_message), &sequence);
        
        /* Check if this is an ACK message */
        int ack_result = perf_handle_ack(clean_message);
        if (ack_result >= 0) {
            /* This was an ACK, don't display as regular message */
            continue;
        }

        if (strncmp(clean_message, "FILE:", 5) == 0) {
            // Parse header
            char fname[256];
            long fsize;
            sscanf(clean_message, "FILE:%255[^:]:%ld", fname, &fsize);

            // Ensure downloads directory exists
            #ifdef _WIN32
                _mkdir("../downloads");
            #else
                mkdir("../downloads", 0755);
            #endif

            // Build full path under ../downloads
            char filepath[512];
            snprintf(filepath, sizeof(filepath), "../downloads/%s", fname);

            FILE *f = fopen(filepath, "wb"); // save in downloads
            if (!f) {
                perror("[ERROR] fopen recv file");
                continue;
            }

            long received = 0;
            while (received < fsize) {
                unsigned char chunk[RECV_BUF];
                int n = recv(conn_sock, chunk, sizeof(chunk), 0);
                if (n <= 0) break;

                unsigned char dec_chunk[RECV_BUF];
                int dec_len = decrypt_message(chunk, n, derived_key, dec_chunk, sizeof(dec_chunk));
                if (dec_len < 0) {
                    fprintf(stderr, "[ERROR] Failed to decrypt chunk.\n");
                    break;
                }

                fwrite(dec_chunk, 1, dec_len, f);
                received += dec_len;
            }

            fclose(f);
            printf("\n[INFO] Received file '%s' (%ld bytes) -> saved in ../downloads\nYou: ", fname, fsize);
            fflush(stdout);
            continue;
        }



        /* Display regular message */
        char ts[16];
        timestamp_now(ts, sizeof(ts));
        printf("\n%s Peer: %s\n", ts, clean_message);
        log_message("%s Peer: %s", ts, clean_message);
        
        /* Send ACK if this was a tracked message */
        if (is_tracked && sequence > 0) {
            perf_send_ack(conn_sock, sequence, derived_key);
        }
        
        printf("You: ");
        fflush(stdout);
        
        /* Auto-display stats every 10 messages */
        perf_auto_display_stats(STATS_DISPLAY_INTERVAL);
    }
    
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

/* ---------- cleanup thread ---------- */
#ifdef _WIN32
  DWORD WINAPI cleanup_fn(LPVOID arg)
#else
  void *cleanup_fn(void *arg)
#endif
{
    (void)arg;
    while (running) {
        sleep_ms(5000); /* Check every 5 seconds */
        perf_cleanup_expired(DEFAULT_TIMEOUT_MS);
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
        printf("\n=== Final Statistics ===\n");
        perf_display_stats();
        running = 0;
        if (conn_sock != sock_invalid) closesocket(conn_sock);
        printf("[INFO] Shutting down...\n");
    }
    return TRUE;
}
#else
static void sigint_handler(int signum) {
    (void)signum;
    printf("\n=== Final Statistics ===\n");
    perf_display_stats();
    running = 0;
    if (conn_sock != sock_invalid) close(conn_sock);
    printf("[INFO] Shutting down...\n");
}
#endif

/* ---------- main flow ---------- */

int main(void) {
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) {
        fprintf(stderr, "[ERROR] WSAStartup failed.\n");
        return 1;
    }
#endif

    mutex_init(&log_mutex);
    perf_init(); /* Initialize performance monitoring */
    
    /* Derive key from password */
    derive_key_from_password(SECRET_KEY, derived_key);

#ifdef _WIN32
    SetConsoleCtrlHandler(console_handler, TRUE);
#else
    struct sigaction sa;
    sa.sa_handler = sigint_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
#endif

    printf("=== P2P Chat System with Performance Monitor ===\n");
    printf("Features: Encryption + Latency Tracking + Statistics\n");
    printf("Commands: 'stats' = show stats, 'reset' = reset stats\n");
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
        /* Auto client mode: detect own device IP and connect to localhost */
        char hostbuffer[256];
        char *IPbuffer;
        struct hostent *host_entry;

        /* Get and display own IP */
        if (gethostname(hostbuffer, sizeof(hostbuffer)) == -1) {
            perror("[ERROR] gethostname failed");
            goto cleanup;
        }
        host_entry = gethostbyname(hostbuffer);
        if (host_entry == NULL) {
            perror("[ERROR] gethostbyname failed");
            goto cleanup;
        }
        IPbuffer = inet_ntoa(*((struct in_addr*) host_entry->h_addr_list[0]));
        printf("[INFO] This device IP: %s\n", IPbuffer);

        /* Ask user for server port */
        char port_s[16];
        int port = 0;
        printf("Enter server port: ");
        fflush(stdout);
        if (!fgets(port_s, sizeof(port_s), stdin)) goto cleanup;
        trim_newline(port_s);
        if (!parse_port(port_s, &port)) {
            fprintf(stderr, "[ERROR] Invalid port number.\n");
            goto cleanup;
        }

        /* Ask user for server IP */
        char ip_input[64];
        printf("Enter server IP (LAN): ");
        fflush(stdout);
        if (!fgets(ip_input, sizeof(ip_input), stdin)) goto cleanup;
        trim_newline(ip_input);

        if (!validate_ip(ip_input)) {
            fprintf(stderr, "[ERROR] Invalid IP format.\n");
            goto cleanup;
        }

        const char *server_ip = ip_input;
        printf("[INFO] Connecting to server at %s:%d\n", server_ip, port);
        conn_sock = start_client(server_ip, port);
        if (conn_sock == sock_invalid) goto cleanup;
    }



    printf("\n[INFO] Performance monitoring enabled!\n");
    printf("[INFO] Your messages will be tracked for latency measurement.\n");
    printf("[INFO] Type 'stats' to view performance statistics.\n");
    printf("[INFO] Type 'reset' to reset statistics.\n\n");

    /* start receiver thread */
    thread_t rx = start_thread(receiver_fn, NULL);
#ifdef _WIN32
    if (!rx) {
        fprintf(stderr, "[ERROR] CreateThread failed.\n");
        goto cleanup;
    }
#endif

    /* start cleanup thread */
    thread_t cleanup_th = start_thread(cleanup_fn, NULL);
#ifdef _WIN32
    if (!cleanup_th) {
        fprintf(stderr, "[ERROR] CreateThread (cleanup) failed.\n");
        goto cleanup;
    }
#endif

    /* sender loop */
    char line[SEND_BUF];
    while (running) {
        printf("You: ");
        fflush(stdout);

        if (!fgets(line, sizeof(line), stdin)) {
            running = 0;
            break;
        }

        trim_newline(line);

        if (line[0] == '\0') {
            printf("[WARN] Cannot send empty message.\n");
            continue;
        }

        /* Handle special commands */
        if (strcmp(line, "stats") == 0) {
            perf_display_stats();
            continue;
        }
        if (strcmp(line, "reset") == 0) {
            perf_reset_stats();
            continue;
        }
        if (strcmp(line, "/history") == 0) {
            view_chat_history();
            continue; // don’t send this as a chat message
        }
        if (strncmp(line, "/sendfile ", 10) == 0) {
            const char *filepath = line + 10;  // Skip "/sendfile "
            send_file(conn_sock, filepath, derived_key);
            continue; // Don't send as chat
        }


        /* Format message with sequence number for tracking */
        char formatted_msg[SEND_BUF];
        int seq = perf_format_message(formatted_msg, sizeof(formatted_msg), line);
        if (seq < 0) {
            fprintf(stderr, "[ERROR] Message too long.\n");
            continue;
        }

        /* Encrypt the formatted message */
        unsigned char encrypted[SEND_BUF];
        int enc_len = encrypt_message((unsigned char*)formatted_msg, 
                                     strlen(formatted_msg), derived_key, 
                                     encrypted, SEND_BUF);
        if (enc_len < 0) {
            fprintf(stderr, "[ERROR] Failed to encrypt message.\n");
            continue;
        }

        /* Send encrypted message */
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

        /* Log + save history */
        char ts[16];
        timestamp_now(ts, sizeof(ts));
        log_message("%s You: %s (seq #%d)", ts, line, seq);
        save_history("YOU", seq, line);
    }

    /* wait for threads */
    join_thread(rx);
    join_thread(cleanup_th);

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

    /* Display final stats */
    printf("\n=== Final Performance Report ===\n");
    perf_display_stats();

#ifdef _WIN32
    WSACleanup();
#endif
    mutex_destroy(&log_mutex);

    return 0;
}
