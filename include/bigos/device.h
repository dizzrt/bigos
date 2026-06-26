#ifndef _BIGOS_DEVICE_H
#define _BIGOS_DEVICE_H

#include <bigos/types.h>
#include <drivers/block/block_device.h>
#include <drivers/rtc/cmos_rtc.h>

NAMESPACE_BIGOS_BEG
namespace device {
    constexpr uint32_t MAX_DEVICES = 16;
    constexpr uint32_t MAX_DRIVERS = 16;

    enum class DeviceClass : uint32_t {
        Block = 1,
        Timer,
        Video,
        Rtc,
        Network,
    };

    // Kernel-internal stable roles. These are registry keys only; they are not
    // syscall, filesystem-visible device numbering, or external validation ABI.
    enum class DeviceRole : uint32_t {
        BootBlock = 1,
        PersistentWritableBlock,
        RamValidationBlock,
        VirtioBlkValidationBlock,
        PitTimer,
        VgaText,
        CmosRtc,
        VirtioNetValidation,
    };

    enum class DeviceState : uint32_t {
        Empty = 0,
        Registered,
        Probing,
        Published,
        ProbeFailed,
    };

    enum class Status : uint32_t {
        Success = 0,
        InvalidArgument,
        Exists,
        NoSpace,
        NotFound,
        NotReady,
        ProbeFailed,
        UnsupportedContext,
    };

    enum class ProbeContext : uint32_t {
        KernelInit = 1,
        OrdinaryBlockable,
    };

    enum class DriverId : uint32_t {
        AtaPio = 1,
        RamBlock,
        VirtioBlk,
        Pit,
        VgaText,
        CmosRtc,
        VirtioNet,
    };

    struct Device;
    using ProbeFn = Status (*)(Device *__device) noexcept;

    struct DeviceDescriptor {
        DeviceClass device_class;
        DeviceRole role;
        uint32_t instance_id;
        uint32_t flags;
        void *private_data;
    };

    struct DriverDescriptor {
        DeviceClass device_class;
        DriverId driver_id;
        ProbeFn probe;
        const char *name;
    };

    struct Device {
        DeviceDescriptor descriptor;
        DeviceState state;
        Status last_status;
        const DriverDescriptor *driver;
        const void *class_interface;
    };

    struct TimerInterface {
        void (*init_channel0)() noexcept;
    };

    struct VideoTextInterface {
        void (*clear_screen)() noexcept;
        void (*write_char)(char __ch, uint8_t __color) noexcept;
        void (*write_string)(const char *__s, uint8_t __color) noexcept;
        void (*fill_cell)(uint8_t __x, uint8_t __y, char __ch, uint8_t __color) noexcept;
        void (*set_cursor)(uint8_t __x, uint8_t __y) noexcept;
    };

    struct RtcInterface {
        bool (*read_time)(driver::rtc::DateTime *__out) noexcept;
    };

    enum class NetworkTxStatus : uint32_t {
        Success = 0,
        InvalidFrame,
        NoSlot,
        NotReady,
        IssueFailure,
        Timeout,
        DeviceError,
    };

    enum class NetworkRxStatus : uint32_t {
        Success = 0,
        NoFrame,
        InvalidArgument,
        NotReady,
        Malformed,
        StaleGeneration,
        OwnershipError,
        DeviceError,
    };

    struct NetworkRxFrame {
        const uint8_t *data;
        uint32_t length;
        uint16_t slot;
        uint32_t generation;
    };

    struct NetworkDiagnostics {
        uint32_t tx_submitted;
        uint32_t tx_completed;
        uint32_t tx_timeout;
        uint32_t tx_rejected;
        uint32_t rx_completed;
        uint32_t rx_malformed;
        uint32_t completion_rejected;
        NetworkTxStatus last_tx_status;
        NetworkRxStatus last_rx_status;
    };

    struct NetworkDevice {
        void *context;
        bool (*ready)(NetworkDevice *__device) noexcept;
        bool (*link_up)(NetworkDevice *__device) noexcept;
        const uint8_t *(*mac)(NetworkDevice *__device) noexcept;
        uint16_t (*mtu)(NetworkDevice *__device) noexcept;
        NetworkTxStatus (*transmit)(
            NetworkDevice *__device, const void *__frame, uint32_t __len, uint64_t __timeout_ticks) noexcept;
        NetworkRxStatus (*poll_rx)(NetworkDevice *__device, NetworkRxFrame *__out) noexcept;
        NetworkRxStatus (*return_rx)(NetworkDevice *__device, const NetworkRxFrame *__frame) noexcept;
        void (*diagnostics)(NetworkDevice *__device, NetworkDiagnostics *__out) noexcept;
    };

    // Bounded kernel-internal registration/probe boundary only. This is not a
    // hotplug layer, PCI/ACPI enumeration, complete bus model, async I/O layer,
    // SMP-safe registry, or user-visible device node API.
    void init() noexcept;
    Status register_device(const DeviceDescriptor *__descriptor) noexcept;
    Status register_driver(const DriverDescriptor *__descriptor) noexcept;
    Status publish(Device *__device, const void *__class_interface) noexcept;
    Status probe(DeviceClass __class, DeviceRole __role, ProbeContext __context) noexcept;
    Status probe_all(ProbeContext __context) noexcept;
    Status find(DeviceClass __class, DeviceRole __role, const Device **__out) noexcept;
    Status find_interface(DeviceClass __class, DeviceRole __role, const void **__out) noexcept;

    driver::block::BlockDevice *block(DeviceRole __role) noexcept;
    const TimerInterface *timer(DeviceRole __role) noexcept;
    const VideoTextInterface *video_text(DeviceRole __role) noexcept;
    const RtcInterface *rtc(DeviceRole __role) noexcept;
    NetworkDevice *network(DeviceRole __role) noexcept;

    void init_pit_timer() noexcept;
    void clear_video_text() noexcept;
    void write_video_text(char __ch, uint8_t __color = 0x0f) noexcept;
    void write_video_text(const char *__s, uint8_t __color = 0x0f) noexcept;
    void fill_video_text_cell(uint8_t __x, uint8_t __y, char __ch, uint8_t __color = 0x0f) noexcept;
    void set_video_text_cursor(uint8_t __x, uint8_t __y) noexcept;
    bool read_rtc_time(driver::rtc::DateTime *__out) noexcept;
}   // namespace device
NAMESPACE_BIGOS_END

#endif   // _BIGOS_DEVICE_H
