#include "bigos_dns.h"

#include "errno.h"
#include "poll.h"
#include "string.h"
#include "sys/socket.h"
#include "unistd.h"

#define DNS_QTYPE_A 1u
#define DNS_QCLASS_IN 1u
#define DNS_RCODE_NXDOMAIN 3u
#define DNS_HEADER_LEN 12u
#define DNS_QUERY_FIXED_LEN 4u
#define DNS_RR_FIXED_LEN 10u

static unsigned short g_dns_next_id = 0x4100u;
static unsigned short g_dns_next_port = 40000u;

static unsigned short read_be16(const unsigned char *p) {
    return (unsigned short)(((unsigned short)p[0] << 8) | (unsigned short)p[1]);
}

static unsigned int read_be32(const unsigned char *p) {
    return ((unsigned int)p[0] << 24) | ((unsigned int)p[1] << 16) | ((unsigned int)p[2] << 8) | (unsigned int)p[3];
}

static void write_be16(unsigned char *p, unsigned short v) {
    p[0] = (unsigned char)(v >> 8);
    p[1] = (unsigned char)v;
}

static int dns_is_name_char(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
}

static unsigned char dns_lower(unsigned char c) {
    if (c >= 'A' && c <= 'Z')
        return (unsigned char)(c - 'A' + 'a');
    return c;
}

static int dns_encode_qname(const char *hostname, unsigned char *out, size_t out_capacity, size_t *out_len) {
    if (hostname == NULL || out == NULL || out_len == NULL || out_capacity == 0) {
        errno = EINVAL;
        return -1;
    }

    size_t total = strlen(hostname);
    if (total == 0 || total > BIGOS_DNS_MAX_HOSTNAME) {
        errno = EINVAL;
        return -1;
    }
    if (hostname[total - 1] == '.') {
        errno = EINVAL;
        return -1;
    }

    size_t pos = 0;
    size_t label_start = 0;
    while (label_start < total) {
        size_t label_len = 0;
        while (label_start + label_len < total && hostname[label_start + label_len] != '.')
            label_len++;
        if (label_len == 0 || label_len > BIGOS_DNS_MAX_LABEL || pos + 1 + label_len + 1 > out_capacity) {
            errno = EINVAL;
            return -1;
        }
        if (hostname[label_start] == '-' || hostname[label_start + label_len - 1] == '-') {
            errno = EINVAL;
            return -1;
        }
        out[pos++] = (unsigned char)label_len;
        for (size_t i = 0; i < label_len; i++) {
            char c = hostname[label_start + i];
            if (!dns_is_name_char(c)) {
                errno = EINVAL;
                return -1;
            }
            out[pos++] = dns_lower((unsigned char)c);
        }
        label_start += label_len;
        if (label_start < total && hostname[label_start] == '.')
            label_start++;
    }

    out[pos++] = 0;
    *out_len = pos;
    return 0;
}

static int dns_build_query(const char *hostname, unsigned short txid, unsigned char *msg, size_t capacity,
                           size_t *out_len, unsigned char *qname, size_t *qname_len) {
    if (msg == NULL || out_len == NULL || qname == NULL || qname_len == NULL || capacity < DNS_HEADER_LEN) {
        errno = EINVAL;
        return -1;
    }
    if (dns_encode_qname(hostname, qname, BIGOS_DNS_MAX_HOSTNAME + 2u, qname_len) != 0)
        return -1;
    if (DNS_HEADER_LEN + *qname_len + DNS_QUERY_FIXED_LEN > capacity) {
        errno = EMSGSIZE;
        return -1;
    }

    memset(msg, 0, capacity);
    write_be16(msg + 0, txid);
    write_be16(msg + 2, 0x0100u);   /* standard recursive query */
    write_be16(msg + 4, 1u);        /* QDCOUNT */
    size_t pos = DNS_HEADER_LEN;
    memcpy(msg + pos, qname, *qname_len);
    pos += *qname_len;
    write_be16(msg + pos, DNS_QTYPE_A);
    write_be16(msg + pos + 2, DNS_QCLASS_IN);
    pos += DNS_QUERY_FIXED_LEN;
    *out_len = pos;
    return 0;
}

static int dns_name_matches_qname(const unsigned char *msg, size_t len, size_t *offset,
                                  const unsigned char *qname, size_t qname_len) {
    size_t pos = *offset;
    size_t qpos = 0;
    unsigned int jumps = 0;
    int jumped = 0;
    size_t next = pos;

    for (;;) {
        if (pos >= len) {
            errno = EINVAL;
            return -1;
        }
        unsigned char label_len = msg[pos];
        if ((label_len & 0xc0u) == 0xc0u) {
            if (pos + 1 >= len || ++jumps > BIGOS_DNS_MAX_COMPRESSION_JUMPS) {
                errno = EINVAL;
                return -1;
            }
            unsigned short ptr = (unsigned short)(((unsigned short)(label_len & 0x3fu) << 8) | msg[pos + 1]);
            if (ptr >= len) {
                errno = EINVAL;
                return -1;
            }
            if (!jumped) {
                next = pos + 2;
                jumped = 1;
            }
            pos = ptr;
            continue;
        }
        if ((label_len & 0xc0u) != 0 || label_len > BIGOS_DNS_MAX_LABEL || pos + 1 + label_len > len ||
            qpos >= qname_len || qname[qpos] != label_len) {
            errno = EINVAL;
            return -1;
        }
        pos++;
        qpos++;
        for (unsigned int i = 0; i < label_len; i++) {
            if (qpos >= qname_len || dns_lower(msg[pos + i]) != qname[qpos + i]) {
                errno = EINVAL;
                return -1;
            }
        }
        pos += label_len;
        qpos += label_len;
        if (label_len == 0)
            break;
    }

    if (qpos != qname_len) {
        errno = EINVAL;
        return -1;
    }
    *offset = jumped ? next : pos;
    return 0;
}

static int dns_skip_any_name(const unsigned char *msg, size_t len, size_t *offset) {
    size_t pos = *offset;
    unsigned int jumps = 0;
    int jumped = 0;
    size_t next = pos;
    for (;;) {
        if (pos >= len) {
            errno = EINVAL;
            return -1;
        }
        unsigned char label_len = msg[pos];
        if ((label_len & 0xc0u) == 0xc0u) {
            if (pos + 1 >= len || ++jumps > BIGOS_DNS_MAX_COMPRESSION_JUMPS) {
                errno = EINVAL;
                return -1;
            }
            unsigned short ptr = (unsigned short)(((unsigned short)(label_len & 0x3fu) << 8) | msg[pos + 1]);
            if (ptr >= len) {
                errno = EINVAL;
                return -1;
            }
            if (!jumped) {
                next = pos + 2;
                jumped = 1;
            }
            pos = ptr;
            continue;
        }
        if ((label_len & 0xc0u) != 0 || label_len > BIGOS_DNS_MAX_LABEL || pos + 1 + label_len > len) {
            errno = EINVAL;
            return -1;
        }
        pos += 1 + label_len;
        if (label_len == 0)
            break;
    }
    *offset = jumped ? next : pos;
    return 0;
}

static int dns_parse_response(const unsigned char *msg, size_t len, unsigned short txid,
                              const unsigned char *qname, size_t qname_len,
                              unsigned int *out_addrs, size_t out_capacity) {
    if (msg == NULL || qname == NULL || out_addrs == NULL || len < DNS_HEADER_LEN) {
        errno = EINVAL;
        return -1;
    }
    if (read_be16(msg + 0) != txid) {
        errno = EINVAL;
        return -1;
    }
    unsigned short flags = read_be16(msg + 2);
    if ((flags & 0x8000u) == 0 || (flags & 0x7800u) != 0) {
        errno = EINVAL;
        return -1;
    }
    if ((flags & 0x0200u) != 0) {
        errno = EMSGSIZE;
        return -1;
    }
    unsigned int rcode = flags & 0x000fu;
    if (rcode == DNS_RCODE_NXDOMAIN) {
        errno = ENOENT;
        return -1;
    }
    if (rcode != 0) {
        errno = EINVAL;
        return -1;
    }
    unsigned short qdcount = read_be16(msg + 4);
    unsigned short ancount = read_be16(msg + 6);
    if (qdcount != 1) {
        errno = EINVAL;
        return -1;
    }

    size_t pos = DNS_HEADER_LEN;
    if (dns_name_matches_qname(msg, len, &pos, qname, qname_len) != 0)
        return -1;
    if (pos + DNS_QUERY_FIXED_LEN > len) {
        errno = EINVAL;
        return -1;
    }
    if (read_be16(msg + pos) != DNS_QTYPE_A || read_be16(msg + pos + 2) != DNS_QCLASS_IN) {
        errno = EINVAL;
        return -1;
    }
    pos += DNS_QUERY_FIXED_LEN;

    size_t count = 0;
    for (unsigned int i = 0; i < ancount; i++) {
        if (dns_skip_any_name(msg, len, &pos) != 0)
            return -1;
        if (pos + DNS_RR_FIXED_LEN > len) {
            errno = EINVAL;
            return -1;
        }
        unsigned short type = read_be16(msg + pos);
        unsigned short klass = read_be16(msg + pos + 2);
        unsigned short rdlen = read_be16(msg + pos + 8);
        pos += DNS_RR_FIXED_LEN;
        if (pos + rdlen > len) {
            errno = EINVAL;
            return -1;
        }
        if (type == DNS_QTYPE_A && klass == DNS_QCLASS_IN) {
            if (rdlen != 4) {
                errno = EINVAL;
                return -1;
            }
            if (count >= out_capacity) {
                errno = ERANGE;
                return -1;
            }
            out_addrs[count++] = read_be32(msg + pos);
        }
        pos += rdlen;
    }
    if (count == 0) {
        errno = ENOENT;
        return -1;
    }
    return (int)count;
}

static int bind_ephemeral_udp(int fd) {
    for (unsigned int attempt = 0; attempt < 1024u; attempt++) {
        unsigned short port = g_dns_next_port++;
        if (g_dns_next_port < 40000u)
            g_dns_next_port = 40000u;
        struct sockaddr_in local;
        local.sin_family = AF_INET;
        local.sin_port = port;
        local.sin_addr = 0;
        if (bind(fd, &local, (socklen_t)sizeof(local)) == 0)
            return 0;
        if (errno != EADDRINUSE)
            return -1;
    }
    errno = EADDRINUSE;
    return -1;
}

int bigos_dns_resolve_ipv4(const char *hostname, unsigned int dns_server_ipv4,
                           unsigned int *out_addrs, size_t out_capacity,
                           unsigned int timeout_ms) {
    if (hostname == NULL || out_addrs == NULL || out_capacity == 0 || dns_server_ipv4 == 0) {
        errno = EINVAL;
        return -1;
    }

    unsigned char query[BIGOS_DNS_MAX_MESSAGE];
    unsigned char qname[BIGOS_DNS_MAX_HOSTNAME + 2u];
    size_t query_len = 0;
    size_t qname_len = 0;
    unsigned short txid = g_dns_next_id++;
    if (g_dns_next_id == 0)
        g_dns_next_id = 0x4100u;
    if (dns_build_query(hostname, txid, query, sizeof(query), &query_len, qname, &qname_len) != 0)
        return -1;

    int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (fd < 0)
        return -1;
    if (bind_ephemeral_udp(fd) != 0) {
        int saved = errno;
        close(fd);
        errno = saved;
        return -1;
    }

    struct sockaddr_in dst;
    dst.sin_family = AF_INET;
    dst.sin_port = BIGOS_DNS_PORT;
    dst.sin_addr = dns_server_ipv4;
    if (sendto(fd, query, query_len, 0, &dst, (socklen_t)sizeof(dst)) != (ssize_t)query_len) {
        int saved = errno;
        close(fd);
        errno = saved;
        return -1;
    }

    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLIN;
    pfd.revents = 0;
    int timeout = timeout_ms > 0x7fffffffu ? 0x7fffffff : (int)timeout_ms;
    int ready = poll(&pfd, 1, timeout);
    if (ready <= 0) {
        int saved = ready == 0 ? ETIMEDOUT : errno;
        close(fd);
        errno = saved;
        return -1;
    }
    if ((pfd.revents & POLLIN) == 0) {
        close(fd);
        errno = ETIMEDOUT;
        return -1;
    }

    unsigned char response[BIGOS_DNS_MAX_MESSAGE];
    struct sockaddr_in src;
    socklen_t src_len = (socklen_t)sizeof(src);
    ssize_t got = recvfrom(fd, response, sizeof(response), 0, &src, &src_len);
    if (got < 0) {
        int saved = errno == EAGAIN || errno == EWOULDBLOCK ? ETIMEDOUT : errno;
        close(fd);
        errno = saved;
        return -1;
    }
    close(fd);
    if (src_len != sizeof(src) || src.sin_family != AF_INET || src.sin_addr != dns_server_ipv4 || src.sin_port != BIGOS_DNS_PORT) {
        errno = EINVAL;
        return -1;
    }
    return dns_parse_response(response, (size_t)got, txid, qname, qname_len, out_addrs, out_capacity);
}

int bigos_getaddr_ipv4(const char *hostname, unsigned int dns_server_ipv4,
                       unsigned int *out_addrs, size_t out_capacity,
                       unsigned int timeout_ms) {
    return bigos_dns_resolve_ipv4(hostname, dns_server_ipv4, out_addrs, out_capacity, timeout_ms);
}
