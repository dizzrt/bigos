#include <bigos/cred.h>

#include <bigos/proc.h>

namespace bigos::cred {
    bool may_signal(const bigos::proc::Process *__actor, const bigos::proc::Process *__target) noexcept {
        // Null inputs are rejected without side effects.
        if (__actor == nullptr || __target == nullptr)
            return false;
        // root (euid == 0) may act on any target.
        if (__actor->euid == ROOT_UID)
            return true;
        // Otherwise the actor's effective uid must match the target's real or
        // effective uid.
        return __actor->euid == __target->uid || __actor->euid == __target->euid;
    }

    bool permits(uint32_t __file_uid, uint32_t __file_gid, uint32_t __mode, uint32_t __req_uid, uint32_t __req_gid,
        Access __access) noexcept {
        // root bypasses the permission bits entirely.
        if (__req_uid == ROOT_UID)
            return true;

        uint32_t read_bit;
        uint32_t write_bit;
        uint32_t execute_bit;
        if (__req_uid == __file_uid) {
            read_bit = S_IRUSR;
            write_bit = S_IWUSR;
            execute_bit = S_IXUSR;
        } else if (__req_gid == __file_gid) {
            read_bit = S_IRGRP;
            write_bit = S_IWGRP;
            execute_bit = S_IXGRP;
        } else {
            read_bit = S_IROTH;
            write_bit = S_IWOTH;
            execute_bit = S_IXOTH;
        }

        switch (__access) {
            case Access::Read:
                return (__mode & read_bit) != 0;
            case Access::Write:
                return (__mode & write_bit) != 0;
            case Access::Execute:
                return (__mode & execute_bit) != 0;
        }
        // Invalid access type: reject, no side effects.
        return false;
    }
}   // namespace bigos::cred
