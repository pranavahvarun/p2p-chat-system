/*
 * utils.h - Header for Part 3: Latency & Performance Monitor
 * 
 * This header defines the interface for latency measurement and 
 * performance monitoring in the P2P chat system.
 */

#ifndef UTILS_H
#define UTILS_H

#include <stdint.h>
#include <stddef.h>
#include "encryption.h"

/* Platform compatibility */
#ifdef _WIN32
    #include <winsock2.h>
    typedef SOCKET sock_t;
#else
    typedef int sock_t;
#endif

/* Configuration constants */
#define MAX_PENDING_MSGS 100
#define MAX_MSG_LEN 512
#define DEFAULT_TIMEOUT_MS 5000
#define STATS_DISPLAY_INTERVAL 10

/* Performance statistics structure */
typedef struct {
    uint32_t total_messages;    /* Total messages sent */
    double total_latency;       /* Sum of all latencies */
    double avg_latency;         /* Average latency in ms */
    double min_latency;         /* Minimum latency in ms */
    double max_latency;         /* Maximum latency in ms */
} perf_stats_t;

/* Pending message structure for tracking */
typedef struct {
    uint32_t sequence;          /* Unique sequence number */
    uint64_t timestamp;         /* Send timestamp in microseconds */
    char content[MAX_MSG_LEN];  /* Original message content */
} pending_msg_t;

/* Function prototypes */

/* Initialize performance monitoring system */
void perf_init(void);

/* Add a message to pending list and get sequence number */
uint32_t perf_add_pending_message(const char *message);

/* Millisecond timestamp wrapper for UDP chat */
uint64_t get_time_ms(void);

/* Mark a message as acknowledged and calculate latency */
int perf_acknowledge_message(uint32_t sequence);

/* Get current performance statistics */
perf_stats_t perf_get_stats(void);

/* Display performance statistics to console */
void perf_display_stats(void);

/* Reset all performance statistics */
void perf_reset_stats(void);

/* Format message with sequence number for tracking */
int perf_format_message(char *buffer, size_t buffer_size, const char *message);

/* Parse incoming message and extract sequence number */
int perf_parse_message(const char *raw_message, char *clean_message, 
                      size_t clean_size, uint32_t *sequence);

/* Send acknowledgment for received message */
int perf_send_ack(sock_t socket, uint32_t sequence, const unsigned char *key);

/* Handle received acknowledgment message */
int perf_handle_ack(const char *message);

/* Cleanup expired pending messages */
void perf_cleanup_expired(uint64_t timeout_ms);

/* Auto-display stats every N messages */
void perf_auto_display_stats(int interval);

/* Get formatted timestamp string */
void perf_get_timestamp_str(char *buffer, size_t buffer_size);

#endif /* UTILS_H */
