/* BigOS minimal netdb.h compatibility surface.
 *
 * This header deliberately exposes only the BigOS bounded IPv4 A-record lookup
 * subset. It does not declare broader address, host, or service databases,
 * resolver globals, or any complete POSIX netdb behavior.
 */
#ifndef _BIGOS_USER_NETDB_H
#define _BIGOS_USER_NETDB_H

#include "bigos_dns.h"

/* Minimal compatibility alias for the BigOS bounded resolver. Arguments and
 * return convention match bigos_dns_resolve_ipv4 exactly.
 */
int bigos_getaddr_ipv4(const char *hostname, unsigned int dns_server_ipv4,
                       unsigned int *out_addrs, size_t out_capacity,
                       unsigned int timeout_ms);

#endif /* _BIGOS_USER_NETDB_H */
