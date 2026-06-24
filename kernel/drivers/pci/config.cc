#include <drivers/pci/config.h>

#include <bigos/io.h>
#include <bigos/percpu.h>

namespace driver::pci {
    namespace {
        constexpr uint16_t CONFIG_ADDRESS_PORT = 0xcf8;
        constexpr uint16_t CONFIG_DATA_PORT = 0xcfc;
        constexpr uint32_t CONFIG_ENABLE = 0x80000000u;

        constexpr uint8_t REG_VENDOR_DEVICE = 0x00;
        constexpr uint8_t REG_COMMAND_STATUS = 0x04;
        constexpr uint8_t REG_CLASS_REVISION = 0x08;
        constexpr uint8_t REG_HEADER_TYPE = 0x0e;
        constexpr uint8_t REG_BAR0 = 0x10;
        constexpr uint8_t REG_CAP_PTR = 0x34;
        constexpr uint16_t STATUS_CAPABILITIES_LIST = 1u << 4;

        bool valid_address(FunctionAddress __address) noexcept {
            return __address.device < 32 && __address.function < 8;
        }

        bool valid_dword_offset(uint8_t __offset) noexcept {
            return (__offset & 0x3u) == 0 && __offset <= 0xfc;
        }

        uint32_t config_address(FunctionAddress __address, uint8_t __offset) noexcept {
            return CONFIG_ENABLE | ((uint32_t)__address.bus << 16) | ((uint32_t)__address.device << 11) |
                   ((uint32_t)__address.function << 8) | (__offset & 0xfcu);
        }

        uint64_t decode_bar_size(uint64_t __mask) noexcept {
            if (__mask == 0)
                return 0;
            return (~__mask) + 1;
        }

        bool cap_pointer_valid(uint8_t __ptr) noexcept {
            return __ptr >= 0x40 && __ptr <= 0xfc && (__ptr & 0x3u) == 0;
        }

        bool visited_capability(uint32_t *__visited, uint8_t __ptr) noexcept {
            const uint32_t word = __ptr / 32;
            const uint32_t bit = __ptr % 32;
            const uint32_t mask = 1u << bit;
            if ((__visited[word] & mask) != 0)
                return true;
            __visited[word] |= mask;
            return false;
        }
    }   // namespace

    bool context_allows_config_access() noexcept {
        const bigos::cpu::LocalState &local = bigos::cpu::current_state();
        return local.irq_nesting_depth == 0 && local.nonblocking_depth == 0 && local.preemption_disable_depth == 0;
    }

    Status read_config32(FunctionAddress __address, uint8_t __offset, uint32_t *__out) noexcept {
        if (__out == nullptr)
            return Status::InvalidArgument;
        if (!context_allows_config_access())
            return Status::UnsupportedContext;
        if (!valid_address(__address) || !valid_dword_offset(__offset))
            return Status::InvalidArgument;

        bigos::outl(CONFIG_ADDRESS_PORT, config_address(__address, __offset));
        *__out = bigos::inl(CONFIG_DATA_PORT);
        return Status::Ok;
    }

    Status read_config16(FunctionAddress __address, uint8_t __offset, uint16_t *__out) noexcept {
        if (__out == nullptr)
            return Status::InvalidArgument;
        if (__offset > 0xfe)
            return Status::InvalidArgument;

        uint32_t value = 0;
        Status status = read_config32(__address, (uint8_t)(__offset & 0xfcu), &value);
        if (status != Status::Ok)
            return status;

        *__out = (uint16_t)((value >> ((__offset & 0x2u) * 8)) & 0xffffu);
        return Status::Ok;
    }

    Status read_config8(FunctionAddress __address, uint8_t __offset, uint8_t *__out) noexcept {
        if (__out == nullptr)
            return Status::InvalidArgument;

        uint32_t value = 0;
        Status status = read_config32(__address, (uint8_t)(__offset & 0xfcu), &value);
        if (status != Status::Ok)
            return status;

        *__out = (uint8_t)((value >> ((__offset & 0x3u) * 8)) & 0xffu);
        return Status::Ok;
    }

    Status write_config32(FunctionAddress __address, uint8_t __offset, uint32_t __value) noexcept {
        if (!context_allows_config_access())
            return Status::UnsupportedContext;
        if (!valid_address(__address) || !valid_dword_offset(__offset))
            return Status::InvalidArgument;

        bigos::outl(CONFIG_ADDRESS_PORT, config_address(__address, __offset));
        bigos::outl(CONFIG_DATA_PORT, __value);
        return Status::Ok;
    }

    Status write_config16(FunctionAddress __address, uint8_t __offset, uint16_t __value) noexcept {
        if (__offset > 0xfe)
            return Status::InvalidArgument;

        uint32_t value = 0;
        const uint8_t dword_offset = (uint8_t)(__offset & 0xfcu);
        Status status = read_config32(__address, dword_offset, &value);
        if (status != Status::Ok)
            return status;

        const uint32_t shift = (__offset & 0x2u) * 8;
        value = (value & ~(0xffffu << shift)) | ((uint32_t)__value << shift);
        return write_config32(__address, dword_offset, value);
    }

    Status probe_device(FunctionAddress __address, DeviceId *__out) noexcept {
        if (__out == nullptr)
            return Status::InvalidArgument;

        uint32_t vendor_device = 0;
        Status status = read_config32(__address, REG_VENDOR_DEVICE, &vendor_device);
        if (status != Status::Ok)
            return status;

        const uint16_t vendor = (uint16_t)(vendor_device & 0xffffu);
        if (vendor == INVALID_VENDOR_ID)
            return Status::NoDevice;

        uint32_t command_status = 0;
        uint32_t class_revision = 0;
        uint8_t header_type = 0;
        status = read_config32(__address, REG_COMMAND_STATUS, &command_status);
        if (status != Status::Ok)
            return status;
        status = read_config32(__address, REG_CLASS_REVISION, &class_revision);
        if (status != Status::Ok)
            return status;
        status = read_config8(__address, REG_HEADER_TYPE, &header_type);
        if (status != Status::Ok)
            return status;

        __out->vendor_id = vendor;
        __out->device_id = (uint16_t)(vendor_device >> 16);
        __out->class_code = (uint8_t)(class_revision >> 24);
        __out->subclass = (uint8_t)(class_revision >> 16);
        __out->prog_if = (uint8_t)(class_revision >> 8);
        __out->header_type = header_type;
        __out->status = (uint16_t)(command_status >> 16);
        return Status::Ok;
    }

    Status read_capabilities(FunctionAddress __address, Capability *__out, uint8_t __capacity, uint8_t *__count) noexcept {
        if (__count == nullptr || (__capacity > 0 && __out == nullptr))
            return Status::InvalidArgument;
        *__count = 0;

        DeviceId device = {};
        Status status = probe_device(__address, &device);
        if (status != Status::Ok)
            return status;
        if ((device.status & STATUS_CAPABILITIES_LIST) == 0)
            return Status::Ok;

        const uint8_t header_layout = device.header_type & 0x7fu;
        if (header_layout != 0x00 && header_layout != 0x01)
            return Status::Ok;

        uint8_t ptr = 0;
        status = read_config8(__address, REG_CAP_PTR, &ptr);
        if (status != Status::Ok)
            return status;
        ptr &= 0xfcu;

        uint32_t visited[8] = {};
        for (uint8_t step = 0; step < MAX_CAPABILITIES && ptr != 0; step++) {
            if (!cap_pointer_valid(ptr) || visited_capability(visited, ptr))
                return Status::BadCapabilityList;

            uint16_t cap_header = 0;
            status = read_config16(__address, ptr, &cap_header);
            if (status != Status::Ok)
                return status;

            if (*__count < __capacity) {
                __out[*__count].id = (uint8_t)(cap_header & 0xffu);
                __out[*__count].offset = ptr;
            }
            (*__count)++;
            ptr = (uint8_t)((cap_header >> 8) & 0xfcu);
        }

        if (ptr != 0)
            return Status::BadCapabilityList;
        return Status::Ok;
    }

    Status read_bar(FunctionAddress __address, uint8_t __bar_index, BarInfo *__out) noexcept {
        if (__out == nullptr)
            return Status::InvalidArgument;
        if (__bar_index >= BAR_COUNT)
            return Status::InvalidArgument;

        DeviceId device = {};
        Status status = probe_device(__address, &device);
        if (status != Status::Ok)
            return status;
        if ((device.header_type & 0x7fu) != 0x00)
            return Status::UnsupportedBar;

        const uint8_t offset = (uint8_t)(REG_BAR0 + __bar_index * 4);
        uint32_t original_low = 0;
        status = read_config32(__address, offset, &original_low);
        if (status != Status::Ok)
            return status;

        __out->index = __bar_index;
        __out->kind = BarKind::None;
        __out->prefetchable = false;
        __out->base = 0;
        __out->size = 0;
        __out->raw_low = original_low;
        __out->raw_high = 0;

        if (original_low == 0)
            return Status::Ok;

        status = write_config32(__address, offset, 0xffffffffu);
        if (status != Status::Ok)
            return status;
        uint32_t size_low = 0;
        status = read_config32(__address, offset, &size_low);
        (void)write_config32(__address, offset, original_low);
        if (status != Status::Ok)
            return status;

        if ((original_low & 0x1u) != 0) {
            const uint32_t base_mask = 0xfffffffcu;
            __out->kind = BarKind::Io;
            __out->base = original_low & base_mask;
            __out->size = decode_bar_size(size_low & base_mask);
            return Status::Ok;
        }

        const uint32_t mem_type = (original_low >> 1) & 0x3u;
        __out->prefetchable = (original_low & 0x8u) != 0;

        if (mem_type == 0x0u) {
            const uint32_t base_mask = 0xfffffff0u;
            __out->kind = BarKind::Memory32;
            __out->base = original_low & base_mask;
            __out->size = decode_bar_size(size_low & base_mask);
            return Status::Ok;
        }

        if (mem_type != 0x2u || __bar_index + 1 >= BAR_COUNT)
            return Status::UnsupportedBar;

        uint32_t original_high = 0;
        status = read_config32(__address, (uint8_t)(offset + 4), &original_high);
        if (status != Status::Ok)
            return status;

        status = write_config32(__address, (uint8_t)(offset + 4), 0xffffffffu);
        if (status != Status::Ok)
            return status;
        status = write_config32(__address, offset, 0xffffffffu);
        if (status != Status::Ok) {
            (void)write_config32(__address, (uint8_t)(offset + 4), original_high);
            return status;
        }

        uint32_t size_high = 0;
        uint32_t size_low_64 = 0;
        status = read_config32(__address, offset, &size_low_64);
        if (status == Status::Ok)
            status = read_config32(__address, (uint8_t)(offset + 4), &size_high);
        (void)write_config32(__address, offset, original_low);
        (void)write_config32(__address, (uint8_t)(offset + 4), original_high);
        if (status != Status::Ok)
            return status;

        __out->kind = BarKind::Memory64;
        __out->raw_high = original_high;
        __out->base = ((uint64_t)original_high << 32) | (original_low & 0xfffffff0u);
        __out->size = decode_bar_size(((uint64_t)size_high << 32) | (size_low_64 & 0xfffffff0u));
        return Status::Ok;
    }

    const char *status_name(Status __status) noexcept {
        switch (__status) {
            case Status::Ok:
                return "ok";
            case Status::InvalidArgument:
                return "invalid-argument";
            case Status::UnsupportedContext:
                return "unsupported-context";
            case Status::NoDevice:
                return "no-device";
            case Status::BadCapabilityList:
                return "bad-capability-list";
            case Status::UnsupportedBar:
                return "unsupported-bar";
            default:
                return "unknown";
        }
    }
}   // namespace driver::pci
