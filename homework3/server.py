import socket
import json
import logging

logging.basicConfig(level=logging.INFO, format='%(asctime)s - %(message)s')


clients = {}

HOST = '0.0.0.0'
PORT = 12345

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind((HOST, PORT))

logging.info(f"Рандеву-сервер запущен на {HOST}:{PORT}")

while True:
    data, addr = sock.recvfrom(1024)
    message = json.loads(data.decode())
    
    command = message.get("command")
    client_name = message.get("name")

    if command == "register":
        private_addr_str = message.get("private_addr")
        private_ip, private_port = private_addr_str.split(':')
        private_addr = (private_ip, int(private_port))
        
        clients[client_name] = {
            "public": addr,
            "private": private_addr
        }
        logging.info(f"Клиент '{client_name}' зарегистрирован. Публичный: {addr}, Приватный: {private_addr}")
        
        response = {"status": "registered"}
        sock.sendto(json.dumps(response).encode(), addr)

    elif command == "connect":
        peer_name = message.get("peer_name")
        logging.info(f"Клиент '{client_name}' хочет соединиться с '{peer_name}'")
        
        if client_name in clients and peer_name in clients:
            client_data = clients[client_name]
            peer_data = clients[peer_name]
            
            response_to_client = {
                "command": "peer_info",
                "peer_name": peer_name,
                "public": f"{peer_data['public'][0]}:{peer_data['public'][1]}",
                "private": f"{peer_data['private'][0]}:{peer_data['private'][1]}"
            }
            sock.sendto(json.dumps(response_to_client).encode(), client_data['public'])
            
            response_to_peer = {
                "command": "peer_info",
                "peer_name": client_name,
                "public": f"{client_data['public'][0]}:{client_data['public'][1]}",
                "private": f"{client_data['private'][0]}:{client_data['private'][1]}"
            }
            sock.sendto(json.dumps(response_to_peer).encode(), peer_data['public'])
        else:
            logging.warning(f"Один из клиентов ('{client_name}' или '{peer_name}') не найден.")
