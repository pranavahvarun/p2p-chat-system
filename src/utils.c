/*
 * utils.c - Part 3: Latency & Performance Monitor
 * 
 * This module adds latency measurement capabilities to the P2P chat system.
 * Features:
 * - Round-trip time measurement for messages
 * - Average latency calculation
 * - Performance statistics display
 * - Message sequence tracking
 * - Timestamp utilities for precise timing
 */

#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

#ifdef _WIN32
    #include <windows.h>
#else
    #include <sys/time.h>
#endif

/* Global performance statistics */
static perf_stats_t g_stats = {0};
static pending_msg_t pending_messages[MAX_PENDING_MSGS];
static int pending_count = 0;
static uint32_t next_sequence = 1;

/* Platform-specific high-resolution timer */
static uint64_t get_timestamp_us(void) {
#ifdef _WIN32
    static LARGE_INTEGER frequency = {0};
    LARGE_INTEGER counter;
    
    if (frequency.QuadPart == 0) {
        QueryPerformanceFrequency(&frequency);
    }
    
    QueryPerformanceCounter(&counter);
    return (uint64_t)((counter.QuadPart * 1000000ULL) / frequency.QuadPart);
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)(tv.tv_sec * 1000000ULL + tv.tv_usec);
#endif
}

/* Millisecond timestamp wrapper for UDP chat */
uint64_t get_time_ms(void) {
    return get_timestamp_us() / 1000;
}

/* Initialize performance monitoring */
void perf_init(void) {
    memset(&g_stats, 0, sizeof(g_stats));
    memset(pending_messages, 0, sizeof(pending_messages));
    pending_count = 0;
    next_sequence = 1;
    
    printf("[PERF] Performance monitoring initialized\n");
}

/* Add a message to pending list for latency tracking */
uint32_t perf_add_pending_message(const char *message) {
    if (pending_count >= MAX_PENDING_MSGS) {
        /* Remove oldest if full */
        memmove(&pending_messages[0], &pending_messages[1], 
                (MAX_PENDING_MSGS - 1) * sizeof(pending_msg_t));
        pending_count--;
    }
    
    pending_msg_t *msg = &pending_messages[pending_count++];
    msg->sequence = next_sequence++;
    msg->timestamp = get_timestamp_us();
    strncpy(msg->content, message, MAX_MSG_LEN - 1);
    msg->content[MAX_MSG_LEN - 1] = '\0';
    
    return msg->sequence;
}

/* Mark a message as acknowledged and calculate latency */
int perf_acknowledge_message(uint32_t sequence) {
    for (int i = 0; i < pending_count; i++) {
        if (pending_messages[i].sequence == sequence) {
            uint64_t now = get_timestamp_us();
            double latency_ms = (now - pending_messages[i].timestamp) / 1000.0;
            
            /* Update statistics */
            g_stats.total_messages++;
            g_stats.total_latency += latency_ms;
            g_stats.avg_latency = g_stats.total_latency / g_stats.total_messages;
            
            if (latency_ms > g_stats.max_latency) {
                g_stats.max_latency = latency_ms;
            }
            if (g_stats.min_latency == 0.0 || latency_ms < g_stats.min_latency) {
                g_stats.min_latency = latency_ms;
            }
            
            /* Remove from pending list */
            memmove(&pending_messages[i], &pending_messages[i + 1],
                    (pending_count - i - 1) * sizeof(pending_msg_t));
            pending_count--;
            
            printf("[PERF] Message #%u RTT: %.2f ms\n", sequence, latency_ms);
            return 1;
        }
    }
    return 0; /* Message not found */
}

/* Get current performance statistics */
perf_stats_t perf_get_stats(void) {
    return g_stats;
}

/* Display performance statistics */
void perf_display_stats(void) {
    printf("\n=== Performance Statistics ===\n");
    printf("Total Messages: %u\n", g_stats.total_messages);
    printf("Average Latency: %.2f ms\n", g_stats.avg_latency);
    printf("Min Latency: %.2f ms\n", g_stats.min_latency);
    printf("Max Latency: %.2f ms\n", g_stats.max_latency);
    printf("Pending Messages: %d\n", pending_count);
    printf("===============================\n\n");
}

/* Reset performance statistics */
void perf_reset_stats(void) {
    memset(&g_stats, 0, sizeof(g_stats));
    memset(pending_messages, 0, sizeof(pending_messages));
    pending_count = 0;
    printf("[PERF] Statistics reset\n");
}

/* Format message with sequence number for tracking */
int perf_format_message(char *buffer, size_t buffer_size, const char *message) {
    uint32_t seq = perf_add_pending_message(message);
    int ret = snprintf(buffer, buffer_size, "SEQ:%u:%s", seq, message);
    
    if (ret >= (int)buffer_size) {
        /* Truncated, remove from pending */
        pending_count--;
        return -1;
    }
    
    return seq;
}

/* Parse incoming message and extract sequence number */
int perf_parse_message(const char *raw_message, char *clean_message, 
                      size_t clean_size, uint32_t *sequence) {
    if (strncmp(raw_message, "SEQ:", 4) == 0) {
        /* This is a tracked message */
        const char *seq_start = raw_message + 4;
        const char *msg_start = strchr(seq_start, ':');
        
        if (msg_start) {
            *sequence = (uint32_t)strtoul(seq_start, NULL, 10);
            msg_start++; /* Skip the ':' */
            strncpy(clean_message, msg_start, clean_size - 1);
            clean_message[clean_size - 1] = '\0';
            return 1; /* Tracked message */
        }
    }
    
    /* Not a tracked message, copy as-is */
    strncpy(clean_message, raw_message, clean_size - 1);
    clean_message[clean_size - 1] = '\0';
    *sequence = 0;
    return 0; /* Regular message */
}

/* Send acknowledgment for received message */
int perf_send_ack(sock_t socket, uint32_t sequence, const unsigned char *key) {
    char ack_msg[64];
    snprintf(ack_msg, sizeof(ack_msg), "ACK:%u", sequence);
    
    /* Encrypt and send acknowledgment */
    unsigned char encrypted[128];
    int enc_len = encrypt_message((unsigned char*)ack_msg, strlen(ack_msg),
                                 key, encrypted, sizeof(encrypted));
    
    if (enc_len < 0) {
        fprintf(stderr, "[PERF] Failed to encrypt ACK\n");
        return -1;
    }
    
#ifdef _WIN32
    int sent = send(socket, (const char*)encrypted, enc_len, 0);
    return (sent == SOCKET_ERROR) ? -1 : 0;
#else
    ssize_t sent = send(socket, (const char*)encrypted, enc_len, 0);
    return (sent < 0) ? -1 : 0;
#endif
}

/* Handle received acknowledgment */
int perf_handle_ack(const char *message) {
    if (strncmp(message, "ACK:", 4) == 0) {
        uint32_t sequence = (uint32_t)strtoul(message + 4, NULL, 10);
        return perf_acknowledge_message(sequence) ? 1 : 0;
    }
    return -1; /* Not an ACK message */
}

/* Cleanup expired pending messages */
void perf_cleanup_expired(uint64_t timeout_ms) {
    uint64_t now = get_timestamp_us();
    uint64_t timeout_us = timeout_ms * 1000;
    
    int removed = 0;
    for (int i = 0; i < pending_count; ) {
        if (now - pending_messages[i].timestamp > timeout_us) {
            /* Message expired */
            printf("[PERF] Message #%u expired (timeout)\n", 
                   pending_messages[i].sequence);
            
            /* Remove from list */
            memmove(&pending_messages[i], &pending_messages[i + 1],
                    (pending_count - i - 1) * sizeof(pending_msg_t));
            pending_count--;
            removed++;
        } else {
            i++;
        }
    }
    
    if (removed > 0) {
        printf("[PERF] Cleaned up %d expired messages\n", removed);
    }
}

/* Auto-display stats every N messages */
void perf_auto_display_stats(int interval) {
    static int last_display = 0;
    
    if (g_stats.total_messages > 0 && 
        (g_stats.total_messages - last_display) >= interval) {
        perf_display_stats();
        last_display = g_stats.total_messages;
    }
}

/* Get formatted timestamp string */
void perf_get_timestamp_str(char *buffer, size_t buffer_size) {
    time_t now = time(NULL);
#ifdef _WIN32
    struct tm tm_;
    localtime_s(&tm_, &now);
    strftime(buffer, buffer_size, "%H:%M:%S", &tm_);
#else
    struct tm tm_;
    localtime_r(&now, &tm_);
    strftime(buffer, buffer_size, "%H:%M:%S", &tm_);
#endif
}
