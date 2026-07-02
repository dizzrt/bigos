from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read_source(relative: str) -> str:
    return (ROOT / relative).read_text(encoding='utf-8')


def test_dns_errno_and_public_headers_are_bounded() -> None:
    kernel_errno = read_source('include/bigos/errno.h')
    user_errno = read_source('user/libc/include/errno.h')
    dns_h = read_source('user/libc/include/bigos_dns.h')
    netdb_h = read_source('user/libc/include/netdb.h')
    libc_h = read_source('user/libc/include/libc.h')

    assert 'constexpr int ETIMEDOUT = 110;' in kernel_errno
    assert '#define ETIMEDOUT   110' in user_errno

    assert 'int bigos_dns_resolve_ipv4(' in dns_h
    assert 'unsigned int dns_server_ipv4' in dns_h
    assert 'unsigned int *out_addrs' in dns_h
    assert 'size_t out_capacity' in dns_h
    assert 'unsigned int timeout_ms' in dns_h
    assert 'host-order IPv4' in dns_h
    assert 'caller-provided output storage' in dns_h
    assert 'not a POSIX resolver' in dns_h

    assert 'int bigos_getaddr_ipv4(' in netdb_h
    for unsupported in ('getaddrinfo', 'gethostbyname', 'freeaddrinfo', 'servent', 'hostent'):
        assert unsupported not in netdb_h
    assert '"bigos_dns.h"' in libc_h


def test_dns_query_encoder_and_hostname_rejection_are_bounded() -> None:
    dns = read_source('user/libc/dns.c')

    assert 'DNS_QTYPE_A 1u' in dns
    assert 'DNS_QCLASS_IN 1u' in dns
    assert 'DNS_HEADER_LEN 12u' in dns
    assert 'BIGOS_DNS_MAX_MESSAGE' in dns
    assert 'BIGOS_DNS_MAX_HOSTNAME' in dns
    assert 'BIGOS_DNS_MAX_LABEL' in dns
    assert 'static int dns_encode_qname(' in dns
    assert 'total == 0 || total > BIGOS_DNS_MAX_HOSTNAME' in dns
    assert "hostname[total - 1] == '.'" in dns
    assert 'label_len == 0 || label_len > BIGOS_DNS_MAX_LABEL' in dns
    assert "hostname[label_start] == '-'" in dns
    assert '!dns_is_name_char(c)' in dns
    assert 'write_be16(msg + 4, 1u);' in dns
    assert 'write_be16(msg + pos, DNS_QTYPE_A);' in dns
    assert 'write_be16(msg + pos + 2, DNS_QCLASS_IN);' in dns


def test_dns_response_parser_covers_matching_compression_and_errors() -> None:
    dns = read_source('user/libc/dns.c')

    assert 'static int dns_parse_response(' in dns
    assert 'read_be16(msg + 0) != txid' in dns
    assert '(flags & 0x8000u) == 0' in dns
    assert '(flags & 0x7800u) != 0' in dns
    assert '(flags & 0x0200u) != 0' in dns
    assert 'errno = EMSGSIZE;' in dns
    assert 'DNS_RCODE_NXDOMAIN' in dns
    assert 'errno = ENOENT;' in dns
    assert 'qdcount != 1' in dns
    assert 'dns_name_matches_qname(msg, len, &pos, qname, qname_len)' in dns
    assert 'dns_skip_any_name(msg, len, &pos)' in dns
    assert '(label_len & 0xc0u) == 0xc0u' in dns
    assert '++jumps > BIGOS_DNS_MAX_COMPRESSION_JUMPS' in dns
    assert 'ptr >= len' in dns
    assert 'rdlen != 4' in dns
    assert 'count >= out_capacity' in dns
    assert 'out_addrs[count++] = read_be32(msg + pos);' in dns


def test_dns_resolver_uses_udp_socket_and_closes_all_paths() -> None:
    dns = read_source('user/libc/dns.c')

    assert 'socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)' in dns
    assert 'bind_ephemeral_udp(fd)' in dns
    assert 'dst.sin_port = BIGOS_DNS_PORT;' in dns
    assert 'sendto(fd, query, query_len, 0, &dst' in dns
    assert 'poll(&pfd, 1, timeout)' in dns
    assert 'recvfrom(fd, response, sizeof(response), 0, &src, &src_len)' in dns
    assert 'saved = ready == 0 ? ETIMEDOUT : errno' in dns
    assert 'errno == EAGAIN || errno == EWOULDBLOCK ? ETIMEDOUT : errno' in dns
    # Each resolver failure branch after fd creation restores errno after closing.
    assert dns.count('close(fd);') >= 5
    assert 'src.sin_addr != dns_server_ipv4 || src.sin_port != BIGOS_DNS_PORT' in dns


def test_dns_smoke_is_default_off_and_registered() -> None:
    xmake_options = read_source('xmake/options.lua')
    xmake_user = read_source('xmake/user_package.lua')
    core = read_source('tools/bigosdev/core.py')
    smoke = read_source('user/smoke/dns_resolver_smoke.c')

    assert 'option("dns_resolver_smoke")' in xmake_options
    option_index = xmake_options.index('option("dns_resolver_smoke")')
    assert 'set_default(false)' in xmake_options[option_index:option_index + 220]
    assert 'has_config("dns_resolver_smoke")' in xmake_user
    assert 'dns_resolver_smoke.c' in xmake_user
    assert "'dns_resolver_smoke'" in core
    assert "expected_marker='BIGOS_DNS_RESOLVER_PASSED'" in core

    assert 'bigos_dns_resolve_ipv4("example.test"' in smoke
    assert '0xc0u' in smoke and '0x0cu' in smoke
    assert 'for (int i = 0; i < 5; i++)' in smoke
    assert 'errno != ETIMEDOUT' in smoke
