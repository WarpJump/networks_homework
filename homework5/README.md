## Компиляция

```bash
g++ main.cpp -lssl -lcrypto -o chat
```

## Использование

### 1. Генерация ключа и сертификата

```bash
openssl req -x509 -newkey rsa:2048 -nodes -keyout key.pem -out cert.pem -days 365
```

### 2. Запуск сервера
```bash
export SSLKEYLOGFILE=./ssl_keys.log

./chat server <port> <cert_file> <key_file>
```

### 3. Запуск клиента


**Формат:**
```bash
export SSLKEYLOGFILE=./ssl_keys.log

./chat client <host> <port>
```

### 4. Расшифровка траффика
**В wireshark:**
Edit -> Preferences -> Protocols -> TSL -> Secret log filename -> указать путь к .log