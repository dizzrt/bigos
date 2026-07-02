/* Default-off BigOS DNS resolver smoke.
 *
 * Runs as PID-1 only when dns_resolver_smoke is selected. It uses a loopback UDP
 * DNS responder in the parent and the public libc resolver in a child, so the
 * success path must build and parse real DNS wire-format packets. It then runs a
 * few no-responder timeout calls; if the resolver leaked its UDP fd/endpoint,
 * the bounded endpoint pool is exhausted and the later calls fail with another
 * errno instead of ETIMEDOUT.
 */
#include "libc.h"
#include "bigos_dns.h"
#include "sys/socket.h"

#define DNS_TEST_IP bigos_ipv4(127, 0, 0, 42)
#define DNS_SERVER_IP bigos_ipv4(127, 0, 0, 1)

static void emit(const char *s) {
    write(1, s, strlen(s));
}

static void fail(const char *why) {
    emit("BIGOS_DNS_RESOLVER_FAILED ");
    emit(why);
    emit("\n");
    exit(1);
}

static void put16(unsigned char *p, unsigned int v) {
    p[0] = (unsigned char)(v >> 8);
    p[1] = (unsigned char)v;
}

static void put32(unsigned char *p, unsigned int v) {
    p[0] = (unsigned char)(v >> 24);
    p[1] = (unsigned char)(v >> 16);
    p[2] = (unsigned char)(v >> 8);
    p[3] = (unsigned char)v;
}

static int build_response(const unsigned char *query, size_t qlen, unsigned char *response, size_t *rlen) {
    if (qlen < 18)
        return -1;
    size_t pos = 12;
    while (pos < qlen && query[pos] != 0)
        pos += (size_t)query[pos] + 1;
    if (pos >= qlen || pos + 5 > qlen)
        return -1;
    size_t question_len = pos + 5 - 12;

    memcpy(response, query, 12 + question_len);
    put16(response + 2, 0x8180u);
    put16(response + 4, 1u);
    put16(response + 6, 1u);
    put16(response + 8, 0u);
    put16(response + 10, 0u);

    pos = 12 + question_len;
    response[pos++] = 0xc0u;
    response[pos++] = 0x0cu;
    put16(response + pos, 1u);
    put16(response + pos + 2, 1u);
    put32(response + pos + 4, 60u);
    put16(response + pos + 8, 4u);
    pos += 10;
    put32(response + pos, DNS_TEST_IP);
    pos += 4;
    *rlen = pos;
    return 0;
}

static void child_resolve(void) {
    unsigned int addrs[2] = {0, 0};
    errno = 0;
    int count = bigos_dns_resolve_ipv4("example.test", DNS_SERVER_IP, addrs, 2, 2000);
    if (count != 1 || addrs[0] != DNS_TEST_IP || errno != 0)
        exit(2);
    exit(0);
}

static void serve_one_query(int server_fd) {
    unsigned char query[512];
    struct sockaddr_in src;
    socklen_t src_len = sizeof(src);
    ssize_t qlen = recvfrom(server_fd, query, sizeof(query), 0, &src, &src_len);
    if (qlen <= 0)
        fail("server-recv");
    if (src_len != sizeof(src) || src.sin_family != AF_INET || src.sin_addr != DNS_SERVER_IP)
        fail("server-src");
    unsigned char response[512];
    size_t rlen = 0;
    if (build_response(query, (size_t)qlen, response, &rlen) != 0)
        fail("server-build");
    if (sendto(server_fd, response, rlen, 0, &src, src_len) != (ssize_t)rlen)
        fail("server-send");
}

static void test_success(void) {
    int server_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (server_fd < 0)
        fail("server-socket");
    struct sockaddr_in server;
    server.sin_family = AF_INET;
    server.sin_port = BIGOS_DNS_PORT;
    server.sin_addr = DNS_SERVER_IP;
    if (bind(server_fd, &server, sizeof(server)) != 0)
        fail("server-bind");

    pid_t pid = fork();
    if (pid < 0)
        fail("fork");
    if (pid == 0)
        child_resolve();

    serve_one_query(server_fd);
    int status = -1;
    if (wait_status(pid, &status) != pid || status != 0)
        fail("child-status");
    close(server_fd);
}

static void test_timeout_no_leak(void) {
    for (int i = 0; i < 5; i++) {
        unsigned int addr = 0;
        errno = 0;
        if (bigos_dns_resolve_ipv4("timeout.test", DNS_SERVER_IP, &addr, 1, 1) != -1)
            fail("timeout-success");
        if (errno != ETIMEDOUT)
            fail("timeout-errno");
    }
}

int main(void) {
    test_success();
    test_timeout_no_leak();
    emit("BIGOS_DNS_RESOLVER_PASSED\n");
    return 0;
}
