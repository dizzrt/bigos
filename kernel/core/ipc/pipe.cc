#include <bigos/ipc/pipe.h>

#include <bigos/memory.h>
#include <bigos/sched.h>

namespace {
    struct Pipe {
        uint8_t buf[bigos::ipc::PIPE_CAPACITY];
        uint32_t head;   // next read index
        uint32_t tail;   // next write index
        uint32_t count;  // bytes currently buffered
        bool read_open;
        bool write_open;
        bigos::sched::WaitQueue read_wq;
        bigos::sched::WaitQueue write_wq;
    };

    struct PipeEnd {
        Pipe *pipe;
        bool is_write;
    };

    // Predicate arguments for the wait queue.
    struct ReadWait {
        Pipe *pipe;
    };
    struct WriteWait {
        Pipe *pipe;
    };

    bool read_ready(void *__arg) noexcept {
        Pipe *pipe = ((ReadWait *)__arg)->pipe;
        return pipe->count > 0 || !pipe->write_open;
    }

    bool write_ready(void *__arg) noexcept {
        Pipe *pipe = ((WriteWait *)__arg)->pipe;
        return pipe->count < bigos::ipc::PIPE_CAPACITY || !pipe->read_open;
    }

    // Frees the pipe once both ends are closed.
    void maybe_free(Pipe *__pipe) noexcept {
        if (!__pipe->read_open && !__pipe->write_open)
            bigos::free(__pipe);
    }

    bigos::vfs::Status pipe_read(
        bigos::vfs::File *__file, void *__dst, size_t __len, size_t *__bytes_read) noexcept {
        if (__bytes_read != nullptr)
            *__bytes_read = 0;
        if (__file == nullptr || __file->private_data == nullptr)
            return bigos::vfs::Status::BadFileDescriptor;
        PipeEnd *end = (PipeEnd *)__file->private_data;
        if (end->is_write)
            return bigos::vfs::Status::BadFileDescriptor;
        Pipe *pipe = end->pipe;
        if (__len == 0)
            return bigos::vfs::Status::Success;
        if (__dst == nullptr)
            return bigos::vfs::Status::InvalidArgument;

        // Block while empty and writers remain open.
        if (pipe->count == 0 && pipe->write_open) {
            if (!bigos::sched::can_block())
                return bigos::vfs::Status::WouldBlock;
            ReadWait wait = {pipe};
            const int rc = bigos::sched::wait_queue_wait_until(&pipe->read_wq, &read_ready, &wait, 0);
            if (rc != bigos::sched::WAIT_OK)
                return bigos::vfs::Status::WouldBlock;
        }

        // Writers all gone and buffer empty -> EOF.
        if (pipe->count == 0)
            return bigos::vfs::Status::Success;

        uint8_t *out = (uint8_t *)__dst;
        size_t done = 0;
        while (done < __len && pipe->count > 0) {
            out[done] = pipe->buf[pipe->head];
            pipe->head = (pipe->head + 1) % bigos::ipc::PIPE_CAPACITY;
            pipe->count--;
            done++;
        }
        // Reading freed space; wake a blocked writer.
        bigos::sched::wake_all(&pipe->write_wq);
        if (__bytes_read != nullptr)
            *__bytes_read = done;
        return bigos::vfs::Status::Success;
    }

    bigos::vfs::Status pipe_write(
        bigos::vfs::File *__file, const void *__src, size_t __len, size_t *__bytes_written) noexcept {
        if (__bytes_written != nullptr)
            *__bytes_written = 0;
        if (__file == nullptr || __file->private_data == nullptr)
            return bigos::vfs::Status::BadFileDescriptor;
        PipeEnd *end = (PipeEnd *)__file->private_data;
        if (!end->is_write)
            return bigos::vfs::Status::BadFileDescriptor;
        Pipe *pipe = end->pipe;
        if (__len == 0)
            return bigos::vfs::Status::Success;
        if (__src == nullptr)
            return bigos::vfs::Status::InvalidArgument;

        const uint8_t *in = (const uint8_t *)__src;
        size_t done = 0;
        while (done < __len) {
            // Reader gone -> broken pipe.
            if (!pipe->read_open)
                return done > 0 ? bigos::vfs::Status::Success : bigos::vfs::Status::BrokenPipe;
            if (pipe->count == bigos::ipc::PIPE_CAPACITY) {
                if (!bigos::sched::can_block())
                    return done > 0 ? bigos::vfs::Status::Success : bigos::vfs::Status::WouldBlock;
                WriteWait wait = {pipe};
                const int rc = bigos::sched::wait_queue_wait_until(&pipe->write_wq, &write_ready, &wait, 0);
                if (rc != bigos::sched::WAIT_OK)
                    return done > 0 ? bigos::vfs::Status::Success : bigos::vfs::Status::WouldBlock;
                continue;
            }
            pipe->buf[pipe->tail] = in[done];
            pipe->tail = (pipe->tail + 1) % bigos::ipc::PIPE_CAPACITY;
            pipe->count++;
            done++;
            // Wake a blocked reader after each chunk of progress.
            bigos::sched::wake_all(&pipe->read_wq);
        }
        if (__bytes_written != nullptr)
            *__bytes_written = done;
        return bigos::vfs::Status::Success;
    }

    bigos::vfs::Status pipe_lseek(
        bigos::vfs::File *, int64_t, int, uint64_t *) noexcept {
        return bigos::vfs::Status::NotSeekable;
    }

    void pipe_close(bigos::vfs::File *__file) noexcept {
        if (__file == nullptr || __file->private_data == nullptr)
            return;
        PipeEnd *end = (PipeEnd *)__file->private_data;
        Pipe *pipe = end->pipe;
        if (end->is_write) {
            pipe->write_open = false;
            bigos::sched::wake_all(&pipe->read_wq);   // unblock readers -> EOF
        } else {
            pipe->read_open = false;
            bigos::sched::wake_all(&pipe->write_wq);   // unblock writers -> EPIPE
        }
        bigos::free(end);
        __file->private_data = nullptr;
        maybe_free(pipe);
    }

    const bigos::vfs::FileOperations PIPE_READ_OPS = {&pipe_read, &pipe_close, nullptr, &pipe_lseek, nullptr, nullptr};
    const bigos::vfs::FileOperations PIPE_WRITE_OPS = {nullptr, &pipe_close, &pipe_write, &pipe_lseek, nullptr, nullptr};
}   // namespace

NAMESPACE_BIGOS_BEG
namespace ipc {
    vfs::Status create(vfs::File **__read_file, vfs::File **__write_file) noexcept {
        if (__read_file == nullptr || __write_file == nullptr)
            return vfs::Status::InvalidArgument;
        *__read_file = nullptr;
        *__write_file = nullptr;

        Pipe *pipe = (Pipe *)bigos::kmalloc(sizeof(Pipe));
        if (pipe == nullptr)
            return vfs::Status::NoMemory;
        pipe->head = 0;
        pipe->tail = 0;
        pipe->count = 0;
        pipe->read_open = true;
        pipe->write_open = true;
        bigos::sched::init_wait_queue(&pipe->read_wq);
        bigos::sched::init_wait_queue(&pipe->write_wq);

        PipeEnd *read_end = (PipeEnd *)bigos::kmalloc(sizeof(PipeEnd));
        PipeEnd *write_end = (PipeEnd *)bigos::kmalloc(sizeof(PipeEnd));
        vfs::File *read_file = (vfs::File *)bigos::kmalloc(sizeof(vfs::File));
        vfs::File *write_file = (vfs::File *)bigos::kmalloc(sizeof(vfs::File));
        if (read_end == nullptr || write_end == nullptr || read_file == nullptr || write_file == nullptr) {
            if (read_end != nullptr)
                bigos::free(read_end);
            if (write_end != nullptr)
                bigos::free(write_end);
            if (read_file != nullptr)
                bigos::free(read_file);
            if (write_file != nullptr)
                bigos::free(write_file);
            bigos::free(pipe);
            return vfs::Status::NoMemory;
        }

        read_end->pipe = pipe;
        read_end->is_write = false;
        write_end->pipe = pipe;
        write_end->is_write = true;

        read_file->ops = &PIPE_READ_OPS;
        read_file->vnode = nullptr;
        read_file->offset = 0;
        read_file->ref_count = 1;
        read_file->readable = true;
        read_file->close_on_exec = false;
        read_file->private_data = read_end;
        read_file->writable = false;

        write_file->ops = &PIPE_WRITE_OPS;
        write_file->vnode = nullptr;
        write_file->offset = 0;
        write_file->ref_count = 1;
        write_file->readable = false;
        write_file->close_on_exec = false;
        write_file->private_data = write_end;
        write_file->writable = true;

        *__read_file = read_file;
        *__write_file = write_file;
        return vfs::Status::Success;
    }

    bool is_pipe_file(const vfs::File *__file) noexcept {
        return __file != nullptr && (__file->ops == &PIPE_READ_OPS || __file->ops == &PIPE_WRITE_OPS);
    }
}   // namespace ipc
NAMESPACE_BIGOS_END
