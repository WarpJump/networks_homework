import sys, logging

from scapy.all import *
from song import TARGET_IP, FAKE_NET_PREFIX, LYRICS

conf.verb = 0
conf.checkIPaddr = False
conf.use_pcap = True

if len(sys.argv) != 3:
    sys.exit("Usage: python3 main.py <iface1> <iface2>")
IF_1, IF_2 = sys.argv[1], sys.argv[2]

REV_PREFIX = ".".join(FAKE_NET_PREFIX.split(".")[::-1])
FAKE_IPS = {t: f"{FAKE_NET_PREFIX}.{t}" for t in LYRICS}
TGT_REV = ".".join(TARGET_IP.split(".")[::-1])
FINAL_HOP = max(LYRICS.keys()) + 1

print(f"BRIDGE: {IF_1} <-> {IF_2}, target ip: {TARGET_IP}")

try:
    socks = {IF_1: conf.L2socket(iface=IF_1), IF_2: conf.L2socket(iface=IF_2)}
except Exception as e:
    sys.exit(f"Socket error: {e}")


def send_dns(pkt, qname, txt, sock):
    resp = DNS(
        id=pkt[DNS].id,
        qr=1,
        aa=1,
        rd=1,
        ra=1,
        qd=pkt[DNS].qd,
        an=DNSRR(rrname=qname, type="PTR", rclass="IN", ttl=300, rdata=txt),
    )
    reply = (
        Ether(src=pkt[Ether].dst, dst=pkt[Ether].src)
        / IP(src=pkt[IP].dst, dst=pkt[IP].src)
        / UDP(sport=pkt[UDP].dport, dport=pkt[UDP].sport)
        / resp
    )
    sock.send(reply)


def handle(pkt):
    if len(pkt) > 1500:
        return

    src_if = pkt.sniffed_on
    dst_if = IF_2 if src_if == IF_1 else IF_1
    sock_in, sock_out = socks.get(src_if), socks.get(dst_if)

    if not sock_out:
        return

    if IP in pkt:
        ip = pkt[IP]

        if src_if == IF_1 and ip.dst == TARGET_IP:
            ttl = ip.ttl
            if ttl in LYRICS or ttl == FINAL_HOP:
                icmp_type, src_ip = (
                    (3, TARGET_IP) if ttl == FINAL_HOP else (11, FAKE_IPS.get(ttl))
                )

                reply = (
                    Ether(src=pkt[Ether].dst, dst=pkt[Ether].src)
                    / IP(src=src_ip, dst=ip.src, ttl=64)
                    / ICMP(type=icmp_type, code=3 if ttl == FINAL_HOP else 0)
                    / ip
                )
                sock_in.send(reply)
                return

        if UDP in pkt and ip.proto == 17 and pkt[UDP].dport == 53 and DNS in pkt:
            try:
                qname = pkt[DNS].qd.qname.decode("utf-8")
                if REV_PREFIX in qname:
                    ttl = int(qname.split(".", 1)[0])
                    if ttl in LYRICS:
                        send_dns(pkt, qname, LYRICS[ttl], sock_in)
                        return
                elif TGT_REV in qname:
                    send_dns(pkt, qname, "You", sock_in)
                    return
            except:
                pass

        del ip.chksum
        if UDP in pkt:
            del pkt[UDP].chksum

    try:
        sock_out.send(pkt)
    except:
        pass


try:
    sniff(iface=[IF_1, IF_2], prn=handle, store=0, filter="ip or arp")
except KeyboardInterrupt:
    pass
finally:
    for s in socks.values():
        s.close()
