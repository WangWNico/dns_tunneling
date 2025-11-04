/* dns-client.c
 * Simple DNS client for DNS tunneling assignment.
 * Encodes messages as Base64 in the first label of a query and sends to DNS server.
 * Decodes A-record IPv4 octets back into ASCII and prints the server reply.
 *
 * Build: gcc -o client dns-client.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define BUFSIZE 512

static const char b64_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

// Simple base64 encode. src is length len. dst must be large enough.
static int b64_encode(const unsigned char *src, int len, char *dst, int dstsize) {
    int i = 0, j = 0;
    unsigned char a3[3];
    unsigned char a4[4];
    while (len--) {
        a3[i++] = *(src++);
        if (i == 3) {
            a4[0] = (a3[0] & 0xfc) >> 2;
            a4[1] = ((a3[0] & 0x03) << 4) + ((a3[1] & 0xf0) >> 4);
            a4[2] = ((a3[1] & 0x0f) << 2) + ((a3[2] & 0xc0) >> 6);
            a4[3] = a3[2] & 0x3f;
            for (i = 0; i < 4; i++) {
                if (j + 1 >= dstsize) return -1;
                dst[j++] = b64_chars[a4[i]];
            }
            i = 0;
        }
    }
    if (i) {
        for (int k = i; k < 3; k++) a3[k] = '\0';
        a4[0] = (a3[0] & 0xfc) >> 2;
        a4[1] = ((a3[0] & 0x03) << 4) + ((a3[1] & 0xf0) >> 4);
        a4[2] = ((a3[1] & 0x0f) << 2) + ((a3[2] & 0xc0) >> 6);
        a4[3] = a3[2] & 0x3f;
        for (int k = 0; k < i + 1; k++) {
            if (j + 1 >= dstsize) return -1;
            dst[j++] = b64_chars[a4[k]];
        }
        while (i++ < 3) {
            if (j + 1 >= dstsize) return -1;
            dst[j++] = '=';
        }
    }
    if (j >= dstsize) return -1;
    dst[j] = '\0';
    return j;
}

// Build DNS query for name like <label>.<domain>
static int build_query(const char *label, const char *domain, unsigned char *buf, int bufsize) {
    if (bufsize < 12) return -1;
    memset(buf, 0, bufsize);
    // header
    uint16_t id = (uint16_t)rand();
    memcpy(buf, &id, 2);
    buf[2] = 0x01; // recursion desired
    buf[5] = 1; // qdcount

    int offset = 12;
    // write label as DNS label
    const char *p = label;
    int labellen = strlen(label);
    if (labellen > 63) return -1;
    buf[offset++] = (unsigned char)labellen;
    memcpy(buf + offset, label, labellen);
    offset += labellen;
    // append domain pieces
    // domain assumed like google.com
    char domcpy[256]; strncpy(domcpy, domain, sizeof(domcpy)); domcpy[255] = '\0';
    char *tok = strtok(domcpy, ".");
    while (tok) {
        int l = strlen(tok);
        buf[offset++] = (unsigned char)l;
        memcpy(buf + offset, tok, l);
        offset += l;
        tok = strtok(NULL, ".");
    }
    buf[offset++] = 0; // end
    // type A and class IN
    buf[offset++] = 0; buf[offset++] = 1; // type A
    buf[offset++] = 0; buf[offset++] = 1; // class IN
    return offset;
}

// Parse A records from response, assuming answers are contiguous A records
static int parse_response(unsigned char *buf, int buflen, char *out, int outsize) {
    if (buflen < 12) return -1;
    int qdcount = (buf[4] << 8) | buf[5];
    int ancount = (buf[6] << 8) | buf[7];
    int offset = 12;
    // skip questions
    for (int i = 0; i < qdcount; ++i) {
        // skip qname
        while (offset < buflen && buf[offset] != 0) {
            offset += 1 + buf[offset];
        }
        offset++; // null
        offset += 4; // type + class
    }
    int outpos = 0;
    for (int i = 0; i < ancount; ++i) {
        // skip name (could be pointer)
        if (offset >= buflen) break;
        if ((buf[offset] & 0xC0) == 0xC0) {
            offset += 2;
        } else {
            while (offset < buflen && buf[offset] != 0) {
                offset += 1 + buf[offset];
            }
            offset++;
        }
        if (offset + 10 > buflen) break;
        uint16_t type = (buf[offset] << 8) | buf[offset+1]; offset += 2;
        uint16_t cls = (buf[offset] << 8) | buf[offset+1]; offset += 2;
        uint32_t ttl = (buf[offset]<<24)|(buf[offset+1]<<16)|(buf[offset+2]<<8)|buf[offset+3]; offset += 4;
        uint16_t rdlen = (buf[offset]<<8)|buf[offset+1]; offset += 2;
        if (type == 1 && rdlen == 4 && offset + 4 <= buflen) {
            if (outpos + 4 >= outsize) break;
            out[outpos++] = buf[offset];
            out[outpos++] = buf[offset+1];
            out[outpos++] = buf[offset+2];
            out[outpos++] = buf[offset+3];
        }
        offset += rdlen;
    }
    // null-terminate and strip padding spaces (0x20)
    int real = outpos;
    while (real > 0 && (out[real-1] == 0x20 || out[real-1] == 0)) real--;
    if (real >= outsize) real = outsize-1;
    out[real] = '\0';
    return real;
}

int main(int argc, char **argv) {
    const char *server = "127.0.0.1";
    int port = 53;
    if (argc >= 2) server = argv[1];
    if (argc >= 3) port = atoi(argv[2]);

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) { perror("socket"); return 1; }

    struct sockaddr_in serv;
    memset(&serv,0,sizeof(serv));
    serv.sin_family = AF_INET;
    serv.sin_port = htons(port);
    inet_pton(AF_INET, server, &serv.sin_addr);

    const char *domain = "google.com";

    char *messages[2] = {"hi", "hello"};
    for (int m = 0; m < 2; ++m) {
        const char *msg = messages[m];
        printf("Message to send: %s\n", msg);
        // include trailing null in encoding as per example
        int msglen = strlen(msg);
        unsigned char buf_in[256];
        memcpy(buf_in, msg, msglen);
        // append a null char to match example Base64 ending 'A'
        buf_in[msglen] = '\0';
        int inlen = msglen + 1;

        char b64[256];
        if (b64_encode(buf_in, inlen, b64, sizeof(b64)) < 0) { fprintf(stderr, "b64 encode fail\n"); return 1; }
        printf("Encoded data to send: %s\n", b64);

        unsigned char query[BUFSIZE];
        int qlen = build_query(b64, domain, query, sizeof(query));
        if (qlen < 0) { fprintf(stderr, "build query fail\n"); return 1; }

        ssize_t sent = sendto(sock, query, qlen, 0, (struct sockaddr*)&serv, sizeof(serv));
        if (sent < 0) { perror("sendto"); return 1; }
        else {
            char saddr[INET_ADDRSTRLEN]; inet_ntop(AF_INET, &serv.sin_addr, saddr, sizeof(saddr));
            printf("Sent %zd bytes to %s:%d\n", sent, saddr, ntohs(serv.sin_port));
        }

        unsigned char resp[BUFSIZE];
        struct sockaddr_in from; socklen_t fromlen = sizeof(from);
        ssize_t r = recvfrom(sock, resp, sizeof(resp), 0, (struct sockaddr*)&from, &fromlen);
        if (r <= 0) { perror("recvfrom"); return 1; }

        char decoded[512];
        int dlen = parse_response(resp, r, decoded, sizeof(decoded));
        if (dlen < 0) { fprintf(stderr, "parse response failed\n"); return 1; }
        printf("Decoded message from server: %s\n", decoded);
    }

    close(sock);
    return 0;
}
