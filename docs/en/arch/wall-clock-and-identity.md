# Wall-Clock Time And Process Identity

This stage adds a minimal wall clock and a minimal process identity/permission
layer on top of the existing monotonic PIT tick and process model. Both are
deliberately small but correct foundations for the later signal and writable
filesystem stages.

## Wall Clock

### One-shot RTC baseline plus monotonic advance

The wall clock is established once at boot and then advanced by the existing
monotonic tick. It does not poll the RTC periodically.

- `bigos::time::init()` runs after the PIT tick is available (after
  `enableIRQ()`) and before `proc::init()`, so process start timestamps observe
  a ready wall clock.
- It reads the CMOS RTC once over ports `0x70` (index) / `0x71` (data) through
  the `driver::rtc` driver, converts the UTC civil date/time to Unix epoch
  seconds (`days_from_civil` plus hour/minute/second), records it as
  `boot_unix_time`, and captures `boot_tick = timer::ticks()`.
- `bigos::time::current_unix_time()` returns
  `boot_unix_time + (timer::ticks() - boot_tick) / TIMER_HZ`. The query path does
  no hardware access, no allocation, and no blocking, and is monotonic
  non-decreasing under the bounded kernel tick model. `boot_unix_time()` returns the
  baseline.

### RTC read correctness

`driver::rtc::read_time()` polls status register A's update-in-progress (UIP)
bit with a bounded cap before reading the time fields, reads status register B to
decide BCD vs binary and 12/24-hour mode, normalizes the fields (BCD via
`(v & 0x0F) + (v >> 4) * 10`, 12-hour PM handling), and validates each field's
range (month 1-12, day 1-31, hour 0-23, minute/second 0-59, year 0-99). The
century is fixed at 2000 (no ACPI FADT century register). The driver registers no
IRQ8 and does no periodic polling; it is a one-shot read-only API.

### Deterministic degradation

If the UIP poll exceeds its bound or any field is out of range, the wall clock
degrades deterministically: `boot_unix_time` becomes the fixed baseline
(`RTC_INVALID_BASELINE = 0`, the Unix epoch), `boot_tick` is still recorded, and
a fixed marker `BIGOS_RTC_INVALID` is emitted to COM1/VGA. It never panics,
blocks, or allocates, and the current-time query stays monotonically usable on
top of the zero baseline.

### Known limitations

The wall clock is a "boot baseline + monotonic advance" approximation. It is UTC
only (no time zone, DST, or leap seconds), has no clock synchronization
(`settimeofday`/`adjtime`/NTP), and drifts from real time over long runs at a
precision bounded by `TIMER_HZ`.

## Process Identity And Permissions

### Identity quad and start timestamp

Each `Process` carries a minimal identity quad `uid`/`gid`/`euid`/`egid` and a
`start_unix_time` wall-clock creation timestamp (appended fields; the earlier
layout is unchanged).

- init (PID 1) and non-fork ELF creation default to root (all zero); each records
  `start_unix_time = current_unix_time()` at creation.
- `fork_current` copies the parent's identity quad field-by-field into the child
  and stamps the child's `start_unix_time` with the fork-time wall clock (the
  child is a new process). This adds no allocation and no new failure path, and
  does not change the COW / reference-count / rollback semantics or the parent
  returns child PID / child returns 0 contract.
- `exec` replaces the image of the same process; it does not change the identity
  quad and does not refresh `start_unix_time`.

Because there is no login or setuid yet, the whole system runs as root; the
fields exist as inheritable, decidable structure for the signal and writable
filesystem stages.

### Pure decision primitives

`include/bigos/cred.h` exposes pure, side-effect-free decision functions and the
POSIX-layout permission-bit constants. They are not wired to any enforcement
point this stage.

- `bigos::cred::may_signal(actor, target)`: allows when `actor->euid` is root,
  otherwise requires `actor->euid` to match `target->uid` or `target->euid`. Null
  inputs return false. This is the basis for a future kill check.
- `bigos::cred::permits(file_uid, file_gid, mode, req_uid, req_gid, access)`:
  root (`req_uid == 0`) is always allowed; otherwise the owner bits apply when
  `req_uid == file_uid`, the group bits when `req_gid == file_gid`, else the
  other bits. An invalid access type returns false. Reused by the future
  writable filesystem stage.

## Validation

The default-off `time_identity_smoke` (`xmake f --time_identity_smoke=y`,
`BIGOS_TIME_IDENTITY_SMOKE`) emits `BIGOS_TIME_IDENTITY_PASSED` /
`BIGOS_TIME_IDENTITY_FAILED`. It covers the wall clock advancing monotonically
over the tick, a non-fork-created process being root with fork's field-by-field
identity inheritance honored, the privilege decision allowing root / an identity
match and rejecting a non-match and null inputs, and the file-access decision.
The existing smoke matrix and default boot markers are unchanged.

Like the other process smokes, this default-off smoke creates and tears down a
user process before init runs, so the smoke build observes a
`BIGOS_INIT_LOAD_FAILED map-failed` afterward; this is a smoke-mode artifact, not
normal-boot behavior. Source-contract / behavior-assertion tests under `tests/`
fix the new syscall numbers, the identity fields, and the decision primitives.
