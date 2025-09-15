#ifndef UDP_CHAT_H
#define UDP_CHAT_H

/**
 * @brief Starts the UDP chat application in server mode.
 *
 * Binds to the specified port and waits for the first message from a client
 * to establish a peer connection, then begins the chat session.
 *
 * @param port The port number to listen on.
 */
void start_udp_chat_server(int port);

/**
 * @brief Starts the UDP chat application in client mode.
 *
 * Configures the application to send messages to the specified server IP
 * and port, then begins the chat session.
 *
 * @param ip The IP address of the peer server.
 * @param port The port number of the peer server.
 */
void start_udp_chat_client(const char* ip, int port);

#endif // UDP_CHAT_H
