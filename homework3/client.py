import socket
import sys
import json
import threading
import time

peer_addr = None
sock = None
my_known_addrs = set()

def get_private_ip():
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("8.8.8.8", 80))
        ip = s.getsockname()[0]
        s.close()
        return ip
    except Exception:
        return "127.0.0.1"

def receiver_thread():
    global peer_addr, sock, my_known_addrs
    while True:
        try:
            data, addr = sock.recvfrom(1024)

            if addr in my_known_addrs:
                continue

            message = data.decode()
            
            try:
                msg_json = json.loads(message)
                command = msg_json.get("command")
                
                if command == "peer_info":
                    peer_name = msg_json['peer_name']
                    public_ip, public_port = msg_json['public'].split(':')
                    private_ip, private_port = msg_json['private'].split(':')
                    
                    public_addr = (public_ip, int(public_port))
                    private_addr = (private_ip, int(private_port))
                    
                    print(f"\r[СИСТЕМА] Получены данные для '{peer_name}'. Начинаю 'пробивку' NAT...")
                    
                    for _ in range(5):
                        punch_msg = "punch"
                        sock.sendto(punch_msg.encode(), public_addr)
                        sock.sendto(punch_msg.encode(), private_addr)
                        time.sleep(0.5)

            except json.JSONDecodeError:
                if message == "punch":
                    if not peer_addr:
                        print(f"\r[СИСТЕМА] 'Пробивка' успешна! Ответ от {addr}")
                        peer_addr = addr
                        ack_msg = "ack"
                        sock.sendto(ack_msg.encode(), peer_addr)

                elif message == "ack":
                     if not peer_addr:
                         print(f"\r[СИСТЕМА] Соединение с {addr} установлено!")
                         peer_addr = addr
                else:
                    print(f"\r[СООБЩЕНИЕ ОТ {addr}]: {message}")
            
            print("Введите команду: ", end="", flush=True)

        except ConnectionResetError:
            continue
        except Exception as e:
            print(f"\rОшибка в потоке приема: {e}")
            break


def main():
    global sock, my_known_addrs
    if len(sys.argv) != 3:
        print("Использование: python p2p_client.py <ваше_имя> <ip_сервера:порт>")
        return

    my_name = sys.argv[1]
    server_ip, server_port = sys.argv[2].split(':')
    server_addr = (server_ip, int(server_port))

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(('0.0.0.0', 0))
    
    my_private_ip = get_private_ip()
    my_private_port = sock.getsockname()[1]
    my_known_addrs.add((my_private_ip, my_private_port))

    register_msg = { "command": "register", "name": my_name, "private_addr": f"{my_private_ip}:{my_private_port}" }
    sock.sendto(json.dumps(register_msg).encode(), server_addr)
    print(f"Отправлен запрос на регистрацию как '{my_name}'...")
    
    sock.settimeout(5)
    try:
        data, _ = sock.recvfrom(1024)
        if json.loads(data.decode()).get("status") == "registered":
            print("[СИСТЕМА] Успешно зарегистрирован на сервере.")
    except socket.timeout:
        print("[СИСТЕМА] Сервер не ответил. Выход.")
        return
    finally:
        sock.settimeout(None)

    recv_thread = threading.Thread(target=receiver_thread, daemon=True)
    recv_thread.start()

    print("Доступные команды: connect <имя_пира>, say <сообщение>, exit")

    while True:
        try:
            cmd_input = input("Введите команду: ")
            
            cleaned_input = cmd_input.strip()
            if not cleaned_input:
                continue

            parts = cleaned_input.split(" ", 1)
            command = parts[0].lower()

            if command == "connect":
                if len(parts) < 2:
                    print("[СИСТЕМА] Укажите имя пира: connect <имя_пира>")
                    continue
                peer_name = parts[1]
                connect_msg = { "command": "connect", "name": my_name, "peer_name": peer_name }
                sock.sendto(json.dumps(connect_msg).encode(), server_addr)
                print(f"Отправлен запрос на соединение с '{peer_name}'...")
            
            elif command == "say":
                if len(parts) < 2:
                    print("[СИСТЕМА] Укажите сообщение: say <сообщение>")
                    continue
                if peer_addr:
                    message = parts[1]
                    sock.sendto(message.encode(), peer_addr)
                else:
                    print("[СИСТЕМА] Сначала установите соединение с помощью команды 'connect'.")
            
            elif command == "exit":
                break
            
            else:
                print("Неизвестная команда.")
        except IndexError:
            print("Неверный формат команды.")
        except KeyboardInterrupt:
            break

    sock.close()
    print("Клиент завершил работу.")

if __name__ == "__main__":
    main()