import socket
import threading
from datetime import datetime
import os

# Create logs directory if it doesn't exist
log_dir = os.path.join(os.path.dirname(os.path.dirname(__file__)), "logs")
os.makedirs(log_dir, exist_ok=True)
log_file = os.path.join(log_dir, "chatlog.txt")

def log_message(text):
    with open(log_file, "a") as f:
        f.write(text + "\n")

def main():
    print("=== P2P Chat System ===")

    mode = input("Start as (server/client)? ").strip().lower()

    if mode not in ['server', 'client']:
        print("[ERROR] Invalid mode. Please choose 'server' or 'client'.")
        return

    if mode == "server":
        port = int(input("Enter port to listen on: "))  # Example: 5000
        conn = start_server(port)

    elif mode == "client":
        import ipaddress

        peer_ip = input("Enter peer IP address: ").strip()
        try:
            ipaddress.ip_address(peer_ip)
        except ValueError:
            print("[ERROR] Invalid IP address format.")
            return

        try:
            peer_port = int(input("Enter peer port: ").strip())
            if peer_port < 1 or peer_port > 65535:
                raise ValueError
        except ValueError:
            print("[ERROR] Invalid port. Must be between 1 and 65535.")
            return

        conn = start_client(peer_ip, peer_port)
    if conn is None:
        return



    threading.Thread(target=receive_messages, args=(conn,), daemon=True).start()
    send_messages(conn)

def start_server(port):
    server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server_socket.bind(('', port))
    server_socket.listen(1)
    print(f"[INFO] Waiting for peer to connect on port {port}...")
    conn, addr = server_socket.accept()
    print(f"[CONNECTED] Peer connected from {addr}")
    return conn

def start_client(peer_ip, peer_port):
    client_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)  # TCP socket
    try:
        client_socket.connect((peer_ip, peer_port))  # Try to connect to server
        print(f"[CONNECTED] Connected to peer at {peer_ip}:{peer_port}")
        return client_socket
    except Exception as e:
        print(f"[ERROR] Connection failed: {e}")
        exit(1)

def send_messages(connection):
    while True:
        try:
            msg = input("You: ").strip()
            if not msg:
                print("[WARN] Cannot send empty message.")
                continue
            connection.send(msg.encode())
            timestamp = f"[{datetime.now().strftime('%H:%M:%S')}]"
            log_message(f"{timestamp} You: {msg}")
        except Exception as e:
            print(f"\n[ERROR] Sending failed: {e}")
            break

def receive_messages(connection):
    while True:
        try:
            msg = connection.recv(1024).decode()
            if msg:
                timestamp = f"[{datetime.now().strftime('%H:%M:%S')}]"
                print(f"\n{timestamp} Peer: {msg}")
                log_message(f"{timestamp} Peer: {msg}")
            else:
                print("\n[INFO] Connection closed by peer.")
                break
        except Exception as e:
            print(f"\n[ERROR] Receiving failed: {e}")
            break


if __name__ == "__main__":
    main()
