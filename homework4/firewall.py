import re
import argparse
from netfilterqueue import NetfilterQueue
from scapy.all import IP, TCP

class L7Firewall:
    def __init__(self, queue_num, rules_file):
        self.queue_num = queue_num
        self.rules = self._load_rules(rules_file)
        self.nfqueue = NetfilterQueue()

    def run(self):
        print(f"[*] Binding to NFQUEUE queue {self.queue_num}...")
        self.nfqueue.bind(self.queue_num, self._process_packet)
        try:
            self.nfqueue.run()
        except KeyboardInterrupt:
            print("\n[*] Shutting down firewall...")
        finally:
            self.nfqueue.unbind()

    def _load_rules(self, filepath):
        print(f"[*] Loading rules from {filepath}...")
        loaded_rules = []
        with open(filepath, 'r') as f:
            for line in f:
                line = line.strip()
                if not line or line.startswith('#'):
                    continue
                parts = line.split()
                if len(parts) < 2 or parts[0].upper() not in ['ACCEPT', 'DROP']:
                    continue
                
                matches = (re.match(r'\[(.+?)=(.+?)\]', p) for p in parts[2:])
                conditions = {m.group(1): m.group(2) for m in matches if m}
                
                loaded_rules.append({
                    'action': parts[0].upper(), 'protocol': parts[1].upper(),
                    'conditions': conditions, 'raw': line
                })
        print(f"[*] {len(loaded_rules)} rules loaded successfully.")
        return loaded_rules

    def _process_packet(self, packet):
        try:
            ip_packet = IP(packet.get_payload())
            if not ip_packet.haslayer(TCP) or not (tcp_payload := bytes(ip_packet[TCP].payload)):
                packet.accept()
                return
        except:
            packet.accept()
            return

        if not (http_data := self._parse_http_payload(tcp_payload)):
            packet.accept()
            return
        
        print(f"[*] Analyzing HTTP Request: {http_data.get('method')} {http_data.get('host')}{http_data.get('uri')}")
        
        for rule in self.rules:
            if rule['protocol'] != 'HTTP': continue
            
            is_match = all(
                (val := http_data.get(field.lower())) is not None and self._check_match(rule_val, val)
                for field, rule_val in rule['conditions'].items()
            )

            if is_match:
                action = rule['action']
                print(f"[!] Rule matched. Action: {action}. Reason: {rule['raw']}")
                packet.drop() if action == 'DROP' else packet.accept()
                return

        packet.accept()
        
    @staticmethod
    def _parse_http_payload(payload_bytes):
        try:
            lines = payload_bytes.decode('utf-8', errors='ignore').split('\r\n')
            if len(lines) < 2 or not (match := re.match(r'([A-Z]+)\s+([^\s]+)\s+HTTP/(\d\.\d)', lines[0])):
                return None

            http_data = {'method': match.group(1), 'uri': match.group(2), 'proto': 'HTTP/' + match.group(3)}
            for line in lines[1:]:
                if not line: break
                if ':' in line:
                    key, value = line.split(':', 1)
                    http_data[key.strip().lower()] = value.strip()
            return http_data if 'host' in http_data else None
        except:
            return None
    
    @staticmethod
    def _check_match(rule_value, packet_value):
        if rule_value.startswith('regex:'):
            return re.search(rule_value[len('regex:'):], packet_value) is not None
        elif '*' in rule_value:
            return re.match('^' + rule_value.replace('.', r'\.').replace('*', '.*') + '$', packet_value) is not None
        return rule_value == packet_value

def main():
    parser = argparse.ArgumentParser(description="A simple L7 firewall using NFQUEUE.")
    parser.add_argument('-q', '--queue-num', type=int, default=5, help="NFQUEUE queue number to bind to.")
    parser.add_argument('-r', '--rules-file', type=str, required=True, help="Path to the firewall rules file.")
    args = parser.parse_args()

    L7Firewall(args.queue_num, args.rules_file).run()

if __name__ == '__main__':
    main()