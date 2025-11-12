# L7-Firewall

Собиралась первая схема с user0 и fw.


## Формат файла правил (`rules.conf`)

Каждое правило описывается в отдельной строке. Пустые строки и строки, начинающиеся с `#`, игнорируются.

## Запуск и использование

1.  **Перед запуском файервола надо выполнить:**
    ```bash
    sudo sysctl -w net.ipv4.ip_forward=1

    sudo iptables -t mangle -A FORWARD -j NFQUEUE --queue-num 5
    ```

2.  **Запуск файрвола:**
    ```bash
    sudo python3 firewall.py --rules-file rules.conf --queue-num 5
    ```

3.  **Тестирование:**
    С клиентской машины последовательно запускать запросы, которые должны быть заблокированы:
    ```bash
    curl example.com/somefile.exe
    ```
    ```bash
    curl ya.ru
    ```
    ```bash
    curl archive.org
    ```
    ```bash
    curl -X POST -H "Content-Type: application/json" -d '{"login":"test"}' http://httpbin.org/post
    ```

    Запрос зависнет, а в консоли файервола появится сообщение о срабатывании правила.
