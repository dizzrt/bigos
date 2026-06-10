#ifndef _BIG_CRED_H
#define _BIG_CRED_H

#include <bigos/types.h>

namespace bigos::proc {
    struct Process;
}   // namespace bigos::proc

namespace bigos::cred {
    // The root user id. A process whose euid is ROOT_UID bypasses the
    // process-privilege and file-access checks below.
    constexpr uint32_t ROOT_UID = 0;

    // POSIX-layout file permission bits (octal). Reused verbatim by the future
    // writable-filesystem stage so owner/group/other r/w/x map to their
    // conventional numeric positions.
    constexpr uint32_t S_IRUSR = 0400;   // owner read
    constexpr uint32_t S_IWUSR = 0200;   // owner write
    constexpr uint32_t S_IXUSR = 0100;   // owner execute
    constexpr uint32_t S_IRGRP = 0040;   // group read
    constexpr uint32_t S_IWGRP = 0020;   // group write
    constexpr uint32_t S_IXGRP = 0010;   // group execute
    constexpr uint32_t S_IROTH = 0004;   // other read
    constexpr uint32_t S_IWOTH = 0002;   // other write
    constexpr uint32_t S_IXOTH = 0001;   // other execute

    // Requested access type for the file-permission decision.
    enum class Access : uint8_t {
        Read = 0,
        Write,
        Execute,
    };

    // Pure decision: may `__actor` perform a privileged operation (e.g. a future
    // kill) on `__target`? Returns true when the actor's euid is root, otherwise
    // requires the actor's euid to match the target's uid or euid. Null inputs
    // return false. No side effects, no allocation, no panic.
    bool may_signal(const bigos::proc::Process *__actor, const bigos::proc::Process *__target) noexcept;

    // Pure decision: is the requested `__access` to a file owned by
    // (__file_uid, __file_gid) with permission bits `__mode` permitted for a
    // requester (__req_uid, __req_gid)? root (req_uid == ROOT_UID) is always
    // allowed. Otherwise the owner bits apply when req_uid == file_uid, the group
    // bits when req_gid == file_gid, else the other bits. An invalid access type
    // returns false. No side effects, no allocation, no panic.
    bool permits(uint32_t __file_uid, uint32_t __file_gid, uint32_t __mode, uint32_t __req_uid, uint32_t __req_gid,
        Access __access) noexcept;
}   // namespace bigos::cred

#endif   // _BIG_CRED_H
