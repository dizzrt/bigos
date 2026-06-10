#ifndef _BIGOS_IPC_PIPE_H
#define _BIGOS_IPC_PIPE_H

#include <bigos/types.h>
#include <bigos/fs/vfs.h>

NAMESPACE_BIGOS_BEG
namespace ipc {
    // Bounded kernel pipe ring buffer. Capacity is fixed and small. A Pipe is
    // shared by exactly two vfs::File objects (a read end and a write end); each
    // end's File.ref_count tracks how many fds reference that end. When an end's
    // reference count reaches zero the end is closed and the peer is woken; the
    // Pipe object itself is freed when both ends are closed.
    constexpr uint32_t PIPE_CAPACITY = 256;

    // Creates a connected read/write File pair. On success *__read_file and
    // *__write_file own one reference each (ref_count == 1) and Status::Success
    // is returned. On allocation failure nothing is published and
    // Status::NoMemory is returned. Non-IRQ / blockable context only.
    bigos::vfs::Status create(bigos::vfs::File **__read_file, bigos::vfs::File **__write_file) noexcept;

    // True when the file object is a pipe end (read or write).
    bool is_pipe_file(const bigos::vfs::File *__file) noexcept;
}   // namespace ipc
NAMESPACE_BIGOS_END

#endif   // _BIGOS_IPC_PIPE_H
