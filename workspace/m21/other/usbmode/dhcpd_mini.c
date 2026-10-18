/* Minimal single-lease DHCP server for the USB network gadget (usb0).
 * Offers 192.168.7.2 to any client; no DNS/router handed out beyond us.
 * usage: dhcpd_mini [iface] [server_ip] [client_ip] */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>

struct dhcp {
    unsigned char op, htype, hlen, hops;
    unsigned int xid;
    unsigned short secs, flags;
    unsigned int ciaddr, yiaddr, siaddr, giaddr;
    unsigned char chaddr[16];
    unsigned char sname[64];
    unsigned char file[128];
    unsigned int magic;
    unsigned char opts[308];
} __attribute__((packed));

static unsigned char *put(unsigned char *p, int code, int len, const void *d)
{
    *p++ = code; *p++ = len; memcpy(p, d, len); return p + len;
}

int main(int argc, char **argv)
{
    const char *ifn = argc > 1 ? argv[1] : "usb0";
    unsigned int sip = inet_addr(argc > 2 ? argv[2] : "192.168.7.1");
    unsigned int cip = inet_addr(argc > 3 ? argv[3] : "192.168.7.2");
    unsigned int mask = inet_addr("255.255.255.0");
    unsigned int lease = htonl(3600);
    int s = socket(AF_INET, SOCK_DGRAM, 0), one = 1;
    struct sockaddr_in a;
    struct dhcp m, r;

    if (s < 0) { perror("socket"); return 1; }
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    setsockopt(s, SOL_SOCKET, SO_BROADCAST, &one, sizeof(one));
    setsockopt(s, SOL_SOCKET, SO_BINDTODEVICE, ifn, strlen(ifn) + 1);
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET; a.sin_port = htons(67); a.sin_addr.s_addr = INADDR_ANY;
    if (bind(s, (struct sockaddr *)&a, sizeof(a)) < 0) { perror("bind"); return 1; }

    for (;;) {
        ssize_t n = recv(s, &m, sizeof(m), 0);
        if (n < 240 || m.op != 1 || m.magic != htonl(0x63825363)) continue;
        int type = 0;
        unsigned char *o = m.opts, *end = (unsigned char *)&m + n;
        while (o < end && *o != 255) {
            if (*o == 0) { o++; continue; }
            if (o + 1 >= end || o + 2 + o[1] > end) break;
            if (*o == 53) type = o[2];
            o += 2 + o[1];
        }
        if (type != 1 && type != 3) continue;
        memset(&r, 0, sizeof(r));
        r.op = 2; r.htype = m.htype; r.hlen = m.hlen; r.xid = m.xid; r.flags = m.flags;
        r.yiaddr = cip; r.siaddr = sip; memcpy(r.chaddr, m.chaddr, 16);
        r.magic = htonl(0x63825363);
        unsigned char *p = r.opts, t = (type == 1) ? 2 : 5;
        p = put(p, 53, 1, &t);
        p = put(p, 54, 4, &sip);
        p = put(p, 51, 4, &lease);
        p = put(p, 1, 4, &mask);
        *p++ = 255;
        struct sockaddr_in d = { .sin_family = AF_INET, .sin_port = htons(68) };
        d.sin_addr.s_addr = INADDR_BROADCAST;
        sendto(s, &r, (char *)p - (char *)&r, 0, (struct sockaddr *)&d, sizeof(d));
    }
}
