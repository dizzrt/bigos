## Validation

- `xmake build user-init-elf`: passed. Builds the default userland libc and affected user programs with the new DNS resolver source included.
- `xmake f --dns_resolver_smoke=y && xmake build user-init-elf`: passed. Builds the default-off DNS resolver PID-1 smoke image.
- `xmake f --dns_resolver_smoke=n && xmake build user-init-elf`: passed. Restores and rebuilds the default userland configuration after the smoke build.
- `uv run pytest tests/test_syscall_entry_source.py::test_user_libc_syscall_and_errno_mirrors_match_kernel_headers tests/test_minimal_dns_client_source.py`: passed. Covers kernel/user errno mirror equality, DNS public API boundaries, query construction checks, response parsing checks, compression handling, UDP resolver integration, timeout mapping, and default-off smoke registration.
- `openspec validate add-minimal-dns-client --strict`: passed.

## Runtime Smoke

- The default-off DNS resolver runtime smoke was added and built, but not booted in QEMU/Bochs in this session. The smoke is designed to validate a loopback UDP DNS responder, real DNS wire-format query/response parsing, compression pointer response names, `ETIMEDOUT` timeout behavior, and repeated timeout cleanup without exhausting the bounded UDP endpoint pool.
- Default boot/userland runtime smoke was not booted in this session. The default userland build passed with `dns_resolver_smoke` disabled, so DNS remains default-off and does not add a DNS dependency to default boot.

## Historical / Unrelated Diagnostics

- `uv run pytest tests/test_syscall_entry_source.py tests/test_user_c_baseline_source.py tests/test_minimal_dns_client_source.py` was not treated as the final signal because `tests/test_user_c_baseline_source.py::test_userland_smoke_runs_smoke_probes_directly_and_through_shell` fails on an existing `userland_smoke.c` source-string expectation for `rmdir` errno output. This failure is unrelated to the DNS resolver changes; the targeted errno and DNS tests pass.

## Residual Risk

- The DNS smoke has compile-time coverage in this session but still needs an emulator run to prove the parent/child UDP loopback responder path at runtime.
- The resolver intentionally remains a bounded IPv4 A-record subset: no hosted resolver database, no caching daemon, no IPv6/AAAA, no CNAME chain following, no TCP fallback, no EDNS, no DNSSEC, no search domains, and no multi-nameserver retry policy.
