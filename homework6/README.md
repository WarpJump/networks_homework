# Network Traceroute MITM

MITM-мост на Python + Scapy, который перехватывает трафик `traceroute` и подменяет маршрут следования пакетов.

## Установка

```bash
pip install scapy
```

## Запуск

**1. На сервере (Bridge):**
Запустить скрипт, указав два сетевых интерфейса для моста, например в gns3 подключить cloud к eth1 и client к eth0:

```bash
# python3 main.py <iface_client> <iface_internet>
python3 main.py eth0 eth1
```

**2. На клиенте:**


```bash
traceroute -q 1 94.131.13.188
# -q 1 нужен чтобы текст печатался быстрее
```
В результате будет напечатан текст песни