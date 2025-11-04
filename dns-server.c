/* dns-server.c
 * Simple DNS server for DNS tunneling assignment.
 * Listens for DNS queries on UDP port 53 (or a provided port) and responds with
 * A records that encode ASCII text in octets of IPv4 addresses.
 *
 * Build: gcc -o server dns-server.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define BUFSIZE 512

// Simple Base64 decode table
static const unsigned char b64_table[256] = {
    [0 ... 255] = 0x80,
    ['A'] = 0, ['B'] = 1, ['C'] = 2, ['D'] = 3, ['E'] = 4, ['F'] = 5, ['G'] = 6, ['H'] = 7,
    ['I'] = 8, ['J'] = 9, ['K'] = 10, ['L'] = 11, ['M'] = 12, ['N'] = 13, ['O'] = 14, ['P'] = 15,
    ['Q'] = 16, ['R'] = 17, ['S'] = 18, ['T'] = 19, ['U'] = 20, ['V'] = 21, ['W'] = 22, ['X'] = 23,
    ['Y'] = 24, ['Z'] = 25,
    ['a'] = 26, ['b'] = 27, ['c'] = 28, ['d'] = 29, ['e'] = 30, ['f'] = 31, ['g'] = 32, ['h'] = 33,
    ['i'] = 34, ['j'] = 35, ['k'] = 36, ['l'] = 37, ['m'] = 38, ['n'] = 39, ['o'] = 40, ['p'] = 41,
    ['q'] = 42, ['r'] = 43, ['s'] = 44, ['t'] = 45, ['u'] = 46, ['v'] = 47, ['w'] = 48, ['x'] = 49,
    ['y'] = 50, ['z'] = 51,
    ['0'] = 52, ['1'] = 53, ['2'] = 54, ['3'] = 55, ['4'] = 56, ['5'] = 57, ['6'] = 58, ['7'] = 59,
    ['8'] = 60, ['9'] = 61, ['+'] = 62, ['/'] = 63, ['='] = 0x81
};

// Decode base64 in-place from src into dst, return length or -1 on error
static int b64_decode(const char *src, unsigned char *dst, int maxdst) {
    int len = 0;
    int val = 0, valb = -8;
    for (; *src; ++src) {
        unsigned char c = (unsigned char)*src;
        unsigned char d = b64_table[c];
        if (d == 0x80) return -1;
        if (d == 0x81) break; // padding
        val = (val << 6) + d;
        valb += 6;
        if (valb >= 0) {
            if (len >= maxdst) return -1;
            dst[len++] = (unsigned char)((val >> valb) & 0xFF);
            valb -= 8;
        }
    }
    return len;
}

// DNS header is 12 bytes
struct dns_header {
    uint16_t id;
    uint16_t flags;
    uint16_t qdcount;
    uint16_t ancount;
    uint16_t nscount;
    uint16_t arcount;
};

// Read a QNAME from the DNS query buffer, write into out as a dot-separated string.
// Returns number of bytes consumed from buffer or -1 on error. outsize includes null.
static int parse_qname(const unsigned char *buf, int buf_len, int offset, char *out, int outsize) {
    int i = offset;
    int opos = 0;
    while (i < buf_len) {
        unsigned char len = buf[i++];
        if (len == 0) { // end
            if (opos < outsize) out[opos] = '\0';
            return i - offset;
        }
        if (len & 0xC0) return -1; // compression not expected
        if (i + len > buf_len) return -1;
        if (opos + len + 1 >= outsize) return -1;
        if (opos != 0) out[opos++] = '.';
        memcpy(out + opos, buf + i, len);
        opos += len;
        i += len;
    }
    return -1;
}

// Build single A record answer for label (name) with IP bytes provided
static int build_answer(unsigned char *resp, int resp_max, int resp_offset,
                        const unsigned char *qname, int qname_len,
                        const unsigned char ipbytes[4], uint16_t ttl) {
    if (resp_offset + qname_len + 12 > resp_max) return -1;
    // copy qname (already in DNS label format)
    memcpy(resp + resp_offset, qname, qname_len);
    resp_offset += qname_len;
    uint16_t type = htons(1); // A
    uint16_t cls = htons(1);
    memcpy(resp + resp_offset, &type, 2); resp_offset += 2;
    memcpy(resp + resp_offset, &cls, 2); resp_offset += 2;
    uint32_t tttl = htonl(ttl);
    memcpy(resp + resp_offset, &tttl, 4); resp_offset += 4;
    uint16_t rdlen = htons(4);
    memcpy(resp + resp_offset, &rdlen, 2); resp_offset += 2;
    memcpy(resp + resp_offset, ipbytes, 4); resp_offset += 4;
    return resp_offset;
}

int main(int argc, char **argv) {
    int port = 53;
    if (argc >= 2) port = atoi(argv[1]);

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) { perror("socket"); return 1; }

    struct sockaddr_in serv, cli;
    memset(&serv,0,sizeof(serv));
    serv.sin_family = AF_INET;
    // Bind explicitly to loopback to avoid namespace/interface mismatches during testing
    if (argc >= 3) {
        // allow user to pass bind address as 2nd arg
        inet_pton(AF_INET, argv[2], &serv.sin_addr);
    } else {
        inet_pton(AF_INET, "127.0.0.1", &serv.sin_addr);
    }
    serv.sin_port = htons(port);

    if (bind(sock, (struct sockaddr*)&serv, sizeof(serv)) < 0) {
        perror("bind"); return 1;
    }

    char bindaddr[INET_ADDRSTRLEN] = "?";
    inet_ntop(AF_INET, &serv.sin_addr, bindaddr, sizeof(bindaddr));
    printf("DNS server listening on %s:%d...\n", bindaddr, port);

    unsigned char buf[BUFSIZE];
    unsigned char resp[BUFSIZE];
    socklen_t cli_len = sizeof(cli);

    while (1) {
        ssize_t n = recvfrom(sock, buf, sizeof(buf), 0, (struct sockaddr*)&cli, &cli_len);
        if (n <= 0) continue;
        char addrstr[INET_ADDRSTRLEN] = "?";
        inet_ntop(AF_INET, &cli.sin_addr, addrstr, sizeof(addrstr));
        printf("Received %zd bytes from %s:%d\n", n, addrstr, ntohs(cli.sin_port));
    if (n < sizeof(struct dns_header)) continue;
        struct dns_header *hdr = (struct dns_header*)buf;
        uint16_t qdcount = ntohs(hdr->qdcount);
        if (qdcount == 0) continue;

        // parse first question
        int offset = sizeof(struct dns_header);
        char qname[256];
        int qname_consumed = parse_qname(buf, n, offset, qname, sizeof(qname));
        if (qname_consumed <= 0) continue;
        int label_len = 0;
        // extract first label (up to first dot)
        char first_label[128];
        const char *dot = strchr(qname, '.');
        if (dot) label_len = dot - qname; else label_len = strlen(qname);
        if (label_len >= (int)sizeof(first_label)) continue;
        strncpy(first_label, qname, label_len);
        first_label[label_len] = '\0';

        printf("Encoded Query Name: %s\n", first_label);

        unsigned char decoded[256];
        int dlen = b64_decode(first_label, decoded, sizeof(decoded));
        int msglen = 0;
        if (dlen < 0) {
            printf("Base64 decode failed\n");
            msglen = 0;
        } else {
            // Trim trailing NUL bytes if present (client may include trailing \0)
            msglen = dlen;
            while (msglen > 0 && decoded[msglen-1] == '\0') msglen--;
            // print decoded as string (ensure null-terminated)
            char s[260];
            int copylen = msglen < 259 ? msglen : 259;
            memcpy(s, decoded, copylen);
            s[copylen] = '\0';
            printf("Decoded Data: %s\n", s);
        }

        // decide response content based on decoded message (use msglen)
        const char *reply = "";
        if (msglen == 2 && strncmp((char*)decoded, "hi", 2) == 0) reply = "hey!";
        else if (msglen == 5 && strncmp((char*)decoded, "hello", 5) == 0) reply = "hello client";
        else reply = "unknown";

        printf("DNS response sent with message \"%s\" encoded in A records.\n", reply);

        // Build response: copy header, set flags
        memcpy(resp, buf, n);
        struct dns_header *rh = (struct dns_header*)resp;
        rh->flags = htons(0x8400); // response, authoritative
        rh->ancount = htons(0); // will update

        // locate original qname in wire format: we can copy from offset
        int qname_wire_len = 0;
        // recompute wire qname length
        int p = sizeof(struct dns_header);
        while (p < n) {
            unsigned char l = buf[p++];
            qname_wire_len++;
            if (l == 0) break;
            p += l; qname_wire_len += l;
        }
        // copy question and follow fields (type/class) to resp
        int resp_off = n; // start appending after original message

        // encode reply into one or more A records, 4 bytes per A
        int rlen = strlen(reply);
        int blocks = (rlen + 3) / 4;
        int ans_count = 0;
        for (int b = 0; b < blocks; ++b) {
            unsigned char ip[4] = {0,0,0,0};
            for (int i = 0; i < 4; ++i) {
                int idx = b*4 + i;
                if (idx < rlen) ip[i] = (unsigned char)reply[idx];
                else ip[i] = 0x20; // space for padding
            }
            int newoff = build_answer(resp, sizeof(resp), resp_off, buf + sizeof(struct dns_header), qname_wire_len, ip, 60);
            if (newoff < 0) break;
            resp_off = newoff;
            ans_count++;
        }

        rh->ancount = htons(ans_count);

        // send response
        ssize_t sent = sendto(sock, resp, resp_off, 0, (struct sockaddr*)&cli, cli_len);
        (void)sent;
    }

    close(sock);
    return 0;
}
