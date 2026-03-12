import socket

TCP_IP = "127.0.0.1"
TCP_PORT = 6000  # pune portul pe care ascultă serverul

try:
    s = socket.create_connection((TCP_IP, TCP_PORT), timeout=3)
    print(f"Conexiune reușită către server {TCP_IP}:{TCP_PORT}")
    s.close()
except ConnectionRefusedError:
    print(f"Serverul nu răspunde pe {TCP_IP}:{TCP_PORT} (10061)")
except socket.timeout:
    print(f"Timeout la conectarea la {TCP_IP}:{TCP_PORT}")
