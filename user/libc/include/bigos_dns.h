/* BigOS bounded DNS resolver API.
 *
 * This is a freestanding-safe BigOS-specific resolver subset for one hostname
 * at a time. It resolves IPv4 A records only, uses host-order IPv4 values,
 * caller-provided output storage, an explicit host-order DNS server IPv4
 * address, and a bounded millisecond timeout. It is not a POSIX resolver, does
 * not read /etc/resolv.conf, and does not provide caching, threads, locale,
 * heap allocation, IPv6, CNAME following, DNSSEC, EDNS, TCP fallback, search
 * domains, or multiple nameserver policy.
 */
#ifndef _BIGOS_USER_BIGOS_DNS_H
#define _BIGOS_USER_BIGOS_DNS_H

#include <sys/types.h>

#define BIGOS_DNS_PORT 53u
#define BIGOS_DNS_MAX_HOSTNAME 253u
#define BIGOS_DNS_MAX_LABEL 63u
#define BIGOS_DNS_MAX_MESSAGE 512u
#define BIGOS_DNS_MAX_COMPRESSION_JUMPS 16u

/* Returns a positive A-record count on success. On failure returns -1 and sets
 * errno: EINVAL for invalid input or malformed DNS data, ERANGE for output
 * capacity exhaustion, ENOENT for NXDOMAIN/no usable A records, ETIMEDOUT for
 * no matching response inside timeout_ms, EMSGSIZE for truncated DNS responses,
 * and ordinary socket errno values for UDP setup/send/receive failures.
 */
int bigos_dns_resolve_ipv4(const char *hostname, unsigned int dns_server_ipv4,
                           unsigned int *out_addrs, size_t out_capacity,
                           unsigned int timeout_ms);

#endif /* _BIGOS_USER_BIGOS_DNS_H */
