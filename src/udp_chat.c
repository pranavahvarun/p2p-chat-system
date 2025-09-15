#define _CRT_SECURE_NO_WARNINGS

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <stdint.h> // For uint32_t, uint64_t

#include "encryption.h"
#include "utils.h"
const char *SECRET_KEY = "admin123";

// Platform-specific headers
#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #include <windows.h>
  typedef SOCKET sock_t;
  typedef int socklen_t;
  #define sock_invalid INVALID_SOCKET
  #define close_socket(s) closesocket(s)
  typedef HANDLE thread_t;
  typedef HANDLE mutex_t;
#else
  #include <unistd.h>
  #include <errno.h>
  #include <arpa/inet.h>
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <pthread.h>
  typedef int sock_t;
  #define sock_invalid -1
  #define close_socket(s) close(s)
  typedef pthread_t thread_t;
  typedef pthread_mutex_t mutex_t;
#endif

/* -------------------- RELIABILITY PROTOCOL & PACKET DEFINITIONS -------------------- */
#define PAYLOAD_SIZE 1024
#define TIMEOUT_MS 2000         // Timeout for retransmission
#define MAX_UNACKED_PACKETS 64  // Max number of messages we can have in flight

// Defines the type of packet being sent

typedef enum {
    PKT_MSG, // "PKT" for Packet
    PKT_ACK,
    PKT_FIN
} PacketType;

// The structure of every packet we send over UDP
typedef struct {
    PacketType type;
    uint32_t   seq_num;
    int        payload_len; // Length of the actual data in the payload
    char       payload[PAYLOAD_SIZE];
} Packet;

/* -------------------- GLOBAL STATE -------------------- */
static volatile int running = 1;
static sock_t sock = sock_invalid;
static struct sockaddr_in peer_addr;
static volatile int peer_addr_known = 0;

// State for our reliability protocol
static uint32_t next_seq_num_to_send = 0;
static uint32_t expected_seq_num_to_recv = 0;

// Buffer for messages sent but not yet acknowledged
static Packet unacked_packets[MAX_UNACKED_PACKETS];
static uint64_t sent_time_ms[MAX_UNACKED_PACKETS];
static int unacked_count = 0;

// Mutexes to protect shared resources
static mutex_t peer_addr_mutex;
static mutex_t unacked_mutex;

/* -------------------- CROSS-PLATFORM UTILITY FUNCTIONS -------------------- */

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

#ifdef _WIN32
  static thread_t start_thread(LPTHREAD_START_ROUTINE fn, void *arg) { return CreateThread(NULL, 0, fn, arg, 0, NULL); }
  static void join_thread(thread_t t) { WaitForSingleObject(t, INFINITE); CloseHandle(t); }
#else
  static thread_t start_thread(void *(*fn)(void*), void *arg) { pthread_t th; pthread_create(&th, NULL, fn, arg); return th; }
  static void join_thread(thread_t t) { pthread_join(t, NULL); }
#endif

static void trim_newline(char *s) { if (!s) return; size_t n = strlen(s); while (n > 0 && (s[n-1] == '\n' || s[n-1] == '\r')) s[--n] = '\0'; }

/* -------------------- SENDER, RECEIVER, AND RETRANSMITTER THREADS -------------------- */

// Thread to handle receiving packets
#ifdef _WIN32
DWORD WINAPI receiver_fn(LPVOID arg)
#else
void *receiver_fn(void *arg)
#endif
{
    Packet rx_packet;
    struct sockaddr_in sender_addr;
    socklen_t sender_len = sizeof(sender_addr);

    while (running) {
        int n = recvfrom(sock, (char*)&rx_packet, sizeof(Packet), 0, (struct sockaddr*)&sender_addr, &sender_len);
        if (n <= 0) continue;

        // If this is the first packet from our peer, store their address
        if (!peer_addr_known) {
            mutex_lock(&peer_addr_mutex);
            memcpy(&peer_addr, &sender_addr, sizeof(peer_addr));
            peer_addr_known = 1;
            char peer_ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &peer_addr.sin_addr, peer_ip, sizeof(peer_ip));
            printf("\n[CONNECTED] Peer is at %s:%d\n", peer_ip, ntohs(peer_addr.sin_port));
            printf("You: ");
            fflush(stdout);
            mutex_unlock(&peer_addr_mutex);
        }

        switch (rx_packet.type) {
            case PKT_MSG: {
                Packet ack_packet;
                ack_packet.type = PKT_ACK;
                ack_packet.seq_num = rx_packet.seq_num;
                sendto(sock, (char*)&ack_packet, sizeof(Packet), 0, (struct sockaddr*)&peer_addr, sizeof(peer_addr));
                
                if (rx_packet.seq_num == expected_seq_num_to_recv) {
                    unsigned char decrypted_payload[PAYLOAD_SIZE];
                    int dec_len = decrypt_message((unsigned char*)rx_packet.payload, rx_packet.payload_len, (unsigned char*)SECRET_KEY, decrypted_payload, PAYLOAD_SIZE);
                    if (dec_len >= 0) {
                        decrypted_payload[dec_len] = '\0';
                        printf("\nPeer: %s\n", (char*)decrypted_payload);
                    }
                    expected_seq_num_to_recv++;
                } // Discard duplicate or out-of-order packets in this simple implementation
                break;
            }
            case PKT_ACK: {
                mutex_lock(&unacked_mutex);
                for (int i = 0; i < unacked_count; i++) {
                    if (unacked_packets[i].seq_num == rx_packet.seq_num) {
                        printf("[INFO] ACK #%u received.\n", rx_packet.seq_num);
                        // Remove from list by replacing with the last element
                        unacked_packets[i] = unacked_packets[unacked_count - 1];
                        sent_time_ms[i] = sent_time_ms[unacked_count - 1];
                        unacked_count--;
                        break;
                    }
                }
                mutex_unlock(&unacked_mutex);
                break;
            }
            case PKT_FIN: {
                printf("\n[INFO] Peer has disconnected. Shutting down.\n");
                running = 0;
                break;
            }
        }
        if (running) {
             printf("You: ");
             fflush(stdout);
        }
    }
    return 0;
}

// Thread to handle sending user input
#ifdef _WIN32
DWORD WINAPI sender_fn(LPVOID arg)
#else
void *sender_fn(void *arg)
#endif
{
    char line[PAYLOAD_SIZE - 64]; // Leave buffer for encryption overhead
    while (running) {
        printf("You: ");
        fflush(stdout);
        if (!fgets(line, sizeof(line), stdin)) {
            running = 0;
            break;
        }
        trim_newline(line);
        if (!running) break;
        if (strlen(line) == 0) continue;

        if (!peer_addr_known) {
            printf("[WARN] Peer address not known yet. Message not sent.\n");
            continue;
        }

        Packet tx_packet;
        tx_packet.type = PKT_MSG;

        // Encrypt the payload
        unsigned char encrypted_payload[PAYLOAD_SIZE];
        int enc_len = encrypt_message((unsigned char*)line, strlen(line), (unsigned char*)SECRET_KEY, encrypted_payload, PAYLOAD_SIZE);
        if (enc_len < 0) {
            fprintf(stderr, "[ERROR] Failed to encrypt message.\n");
            continue;
        }
        memcpy(tx_packet.payload, encrypted_payload, enc_len);
        tx_packet.payload_len = enc_len;

        mutex_lock(&unacked_mutex);
        if (unacked_count >= MAX_UNACKED_PACKETS) {
            printf("[WARN] Too many unacknowledged packets. Please wait.\n");
            mutex_unlock(&unacked_mutex);
            continue;
        }
        tx_packet.seq_num = next_seq_num_to_send++;
        unacked_packets[unacked_count] = tx_packet;
        sent_time_ms[unacked_count] = get_time_ms();
        unacked_count++;
        mutex_unlock(&unacked_mutex);
        
        printf("[INFO] Sending MSG #%u...\n", tx_packet.seq_num);
        sendto(sock, (char*)&tx_packet, sizeof(Packet), 0, (struct sockaddr*)&peer_addr, sizeof(peer_addr));
    }
    return 0;
}

// Thread to handle retransmitting lost packets
#ifdef _WIN32
DWORD WINAPI retransmitter_fn(LPVOID arg)
#else
void *retransmitter_fn(void *arg)
#endif
{
    while(running) {
        Sleep(100); // Check for timeouts every 100ms
        mutex_lock(&unacked_mutex);
        uint64_t now = get_time_ms();
        for (int i = 0; i < unacked_count; i++) {
            if (now - sent_time_ms[i] > TIMEOUT_MS) {
                printf("[TIMEOUT] Retrying MSG #%u...\n", unacked_packets[i].seq_num);
                sendto(sock, (char*)&unacked_packets[i], sizeof(Packet), 0, (struct sockaddr*)&peer_addr, sizeof(peer_addr));
                sent_time_ms[i] = now; // Update send time
            }
        }
        mutex_unlock(&unacked_mutex);
    }
    return 0;
}


/* -------------------- MAIN APPLICATION LOGIC -------------------- */
int main(void) {
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) { fprintf(stderr, "[ERROR] WSAStartup failed.\n"); return 1; }
#endif

    mutex_init(&peer_addr_mutex);
    mutex_init(&unacked_mutex);

    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == sock_invalid) { perror("[ERROR] socket creation failed"); return 1; }

    printf("=== P2P UDP Chat (w/ Reliability) ===\nStart as (server/client)? ");
    char mode[32];
    fgets(mode, sizeof(mode), stdin);
    trim_newline(mode);

    int port;
    printf("Enter port number: ");
    char port_s[32];
    fgets(port_s, sizeof(port_s), stdin);
    trim_newline(port_s);
    port = atoi(port_s);

    if (strcmp(mode, "server") == 0) {
        struct sockaddr_in server_addr;
        memset(&server_addr, 0, sizeof(server_addr));
        server_addr.sin_family = AF_INET;
        server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
        server_addr.sin_port = htons(port);

        if (bind(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
            perror("[ERROR] bind failed");
            close_socket(sock);
            return 1;
        }
        printf("[INFO] Server listening on port %d. Waiting for client...\n", port);
    } else {
        char ip_str[64];
        printf("Enter server IP address: ");
        fgets(ip_str, sizeof(ip_str), stdin);
        trim_newline(ip_str);
        
        memset(&peer_addr, 0, sizeof(peer_addr));
        peer_addr.sin_family = AF_INET;
        peer_addr.sin_port = htons(port);
        inet_pton(AF_INET, ip_str, &peer_addr.sin_addr);
        peer_addr_known = 1;
        printf("[INFO] Client ready. Type a message to begin.\n");
    }
    
    // Start all threads
    thread_t rx_thread = start_thread(receiver_fn, NULL);
    thread_t tx_thread = start_thread(sender_fn, NULL);
    thread_t rt_thread = start_thread(retransmitter_fn, NULL);

    // Wait for threads to complete
    join_thread(rx_thread);
    join_thread(tx_thread);
    join_thread(rt_thread);

    // Cleanup
    if (peer_addr_known) {
        Packet fin_packet;
        fin_packet.type = PKT_FIN;
        fin_packet.seq_num = next_seq_num_to_send;
        sendto(sock, (char*)&fin_packet, sizeof(Packet), 0, (struct sockaddr*)&peer_addr, sizeof(peer_addr));
    }
    
    close_socket(sock);
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}
