#include <bigos/device.h>

#include <bigos/sched.h>
#include <drivers/block/ata_pio.h>
#include <drivers/block/ram_block_device.h>
#include <drivers/timer/pit.h>
#include <drivers/video/vga.h>

namespace {
    struct Registry {
        bigos::device::Device devices[bigos::device::MAX_DEVICES];
        bigos::device::DriverDescriptor drivers[bigos::device::MAX_DRIVERS];
        uint32_t device_count;
        uint32_t driver_count;
        bool initialized;
    };

    Registry g_registry = {};
    driver::block::AtaPioDevice g_boot_ata = {};
    driver::block::AtaPioDevice g_persistent_ata = {};
    driver::block::RamBlockDevice g_ram_validation_block = {};
    uint8_t g_ram_validation_storage
        [driver::block::RAM_BLOCK_DEFAULT_SECTORS * driver::block::DEFAULT_SECTOR_SIZE] = {};

    void pit_init_channel0() noexcept {
        driver::timer::pit::init_channel0();
    }

    void vga_clear_screen() noexcept {
        driver::video::vga::clear_screen();
    }

    void vga_write_char(char __ch, uint8_t __color) noexcept {
        driver::video::vga::write(__ch, __color);
    }

    void vga_write_string(const char *__s, uint8_t __color) noexcept {
        driver::video::vga::write(__s, __color);
    }

    const bigos::device::TimerInterface g_pit_interface = {&pit_init_channel0};
    const bigos::device::VideoTextInterface g_vga_interface = {&vga_clear_screen, &vga_write_char, &vga_write_string};
    const bigos::device::RtcInterface g_rtc_interface = {&driver::rtc::read_time};

    bool same_device_identity(const bigos::device::DeviceDescriptor &__a,
        const bigos::device::DeviceDescriptor &__b) noexcept {
        return __a.device_class == __b.device_class && __a.role == __b.role && __a.instance_id == __b.instance_id;
    }

    bool same_driver_identity(const bigos::device::DriverDescriptor &__a,
        const bigos::device::DriverDescriptor &__b) noexcept {
        return __a.device_class == __b.device_class && __a.driver_id == __b.driver_id;
    }

    bool context_allows_probe(bigos::device::ProbeContext __context) noexcept {
        if (__context == bigos::device::ProbeContext::KernelInit)
            return true;
        return bigos::sched::can_block();
    }

    bigos::device::Device *find_mutable(bigos::device::DeviceClass __class,
        bigos::device::DeviceRole __role) noexcept {
        for (uint32_t i = 0; i < g_registry.device_count; i++) {
            bigos::device::Device &device = g_registry.devices[i];
            if (device.descriptor.device_class == __class && device.descriptor.role == __role)
                return &device;
        }
        return nullptr;
    }

    bool driver_supports_role(const bigos::device::DriverDescriptor &__driver,
        bigos::device::DeviceRole __role) noexcept {
        using DriverId = bigos::device::DriverId;
        using DeviceRole = bigos::device::DeviceRole;
        switch (__driver.driver_id) {
            case DriverId::AtaPio:
                return __role == DeviceRole::BootBlock || __role == DeviceRole::PersistentWritableBlock;
            case DriverId::RamBlock:
                return __role == DeviceRole::RamValidationBlock;
            case DriverId::Pit:
                return __role == DeviceRole::PitTimer;
            case DriverId::VgaText:
                return __role == DeviceRole::VgaText;
            case DriverId::CmosRtc:
                return __role == DeviceRole::CmosRtc;
            default:
                return false;
        }
    }

    const bigos::device::DriverDescriptor *matching_driver(bigos::device::DeviceClass __class,
        bigos::device::DeviceRole __role) noexcept {
        for (uint32_t i = 0; i < g_registry.driver_count; i++) {
            if (g_registry.drivers[i].device_class == __class && driver_supports_role(g_registry.drivers[i], __role))
                return &g_registry.drivers[i];
        }
        return nullptr;
    }

    bigos::device::Status ata_probe(bigos::device::Device *__device) noexcept {
        if (__device == nullptr)
            return bigos::device::Status::InvalidArgument;
        driver::block::AtaPioDevice *ata = (driver::block::AtaPioDevice *)__device->descriptor.private_data;
        if (ata == nullptr)
            return bigos::device::Status::InvalidArgument;

        if (__device->descriptor.role == bigos::device::DeviceRole::BootBlock) {
            driver::block::ata_pio_primary_master_init(ata);
        } else if (__device->descriptor.role == bigos::device::DeviceRole::PersistentWritableBlock) {
            driver::block::ata_pio_persistent_test_init(ata);
        } else {
            return bigos::device::Status::NotFound;
        }

        return bigos::device::publish(__device, &ata->block);
    }

    bigos::device::Status ram_block_probe(bigos::device::Device *__device) noexcept {
        if (__device == nullptr || __device->descriptor.role != bigos::device::DeviceRole::RamValidationBlock)
            return bigos::device::Status::InvalidArgument;
        driver::block::RamBlockDevice *ram = (driver::block::RamBlockDevice *)__device->descriptor.private_data;
        if (ram == nullptr)
            return bigos::device::Status::InvalidArgument;

        const driver::block::BlockStatus status = driver::block::ram_block_init(
            ram, g_ram_validation_storage, driver::block::RAM_BLOCK_DEFAULT_SECTORS, driver::block::DEFAULT_SECTOR_SIZE);
        if (status != driver::block::BlockStatus::Success)
            return bigos::device::Status::ProbeFailed;
        return bigos::device::publish(__device, &ram->block);
    }

    bigos::device::Status pit_probe(bigos::device::Device *__device) noexcept {
        if (__device == nullptr || __device->descriptor.role != bigos::device::DeviceRole::PitTimer)
            return bigos::device::Status::InvalidArgument;
        return bigos::device::publish(__device, &g_pit_interface);
    }

    bigos::device::Status vga_probe(bigos::device::Device *__device) noexcept {
        if (__device == nullptr || __device->descriptor.role != bigos::device::DeviceRole::VgaText)
            return bigos::device::Status::InvalidArgument;
        return bigos::device::publish(__device, &g_vga_interface);
    }

    bigos::device::Status rtc_probe(bigos::device::Device *__device) noexcept {
        if (__device == nullptr || __device->descriptor.role != bigos::device::DeviceRole::CmosRtc)
            return bigos::device::Status::InvalidArgument;
        return bigos::device::publish(__device, &g_rtc_interface);
    }

    void register_builtin_devices() noexcept {
        using namespace bigos::device;
        const DeviceDescriptor devices[] = {
            {DeviceClass::Block, DeviceRole::BootBlock, 0, 0, &g_boot_ata},
            {DeviceClass::Block, DeviceRole::PersistentWritableBlock, 1, 0, &g_persistent_ata},
            {DeviceClass::Block, DeviceRole::RamValidationBlock, 2, 0, &g_ram_validation_block},
            {DeviceClass::Timer, DeviceRole::PitTimer, 0, 0, nullptr},
            {DeviceClass::Video, DeviceRole::VgaText, 0, 0, nullptr},
            {DeviceClass::Rtc, DeviceRole::CmosRtc, 0, 0, nullptr},
        };
        for (uint32_t i = 0; i < sizeof(devices) / sizeof(devices[0]); i++)
            (void)register_device(&devices[i]);
    }

    void register_builtin_drivers() noexcept {
        using namespace bigos::device;
        const DriverDescriptor drivers[] = {
            {DeviceClass::Block, DriverId::AtaPio, &ata_probe, "ata-pio"},
            {DeviceClass::Block, DriverId::RamBlock, &ram_block_probe, "ram-block-validation"},
            {DeviceClass::Timer, DriverId::Pit, &pit_probe, "pit"},
            {DeviceClass::Video, DriverId::VgaText, &vga_probe, "vga-text"},
            {DeviceClass::Rtc, DriverId::CmosRtc, &rtc_probe, "cmos-rtc"},
        };
        for (uint32_t i = 0; i < sizeof(drivers) / sizeof(drivers[0]); i++)
            (void)register_driver(&drivers[i]);
    }
}   // namespace

NAMESPACE_BIGOS_BEG
namespace device {
    void init() noexcept {
        if (g_registry.initialized)
            return;
        g_registry = {};
        register_builtin_devices();
        register_builtin_drivers();
        g_registry.initialized = true;
    }

    Status register_device(const DeviceDescriptor *__descriptor) noexcept {
        if (__descriptor == nullptr)
            return Status::InvalidArgument;
        for (uint32_t i = 0; i < g_registry.device_count; i++) {
            if (same_device_identity(g_registry.devices[i].descriptor, *__descriptor))
                return Status::Exists;
        }
        if (g_registry.device_count >= MAX_DEVICES)
            return Status::NoSpace;
        Device &slot = g_registry.devices[g_registry.device_count++];
        slot.descriptor = *__descriptor;
        slot.state = DeviceState::Registered;
        slot.last_status = Status::Success;
        slot.driver = nullptr;
        slot.class_interface = nullptr;
        return Status::Success;
    }

    Status register_driver(const DriverDescriptor *__descriptor) noexcept {
        if (__descriptor == nullptr || __descriptor->probe == nullptr)
            return Status::InvalidArgument;
        for (uint32_t i = 0; i < g_registry.driver_count; i++) {
            if (same_driver_identity(g_registry.drivers[i], *__descriptor))
                return Status::Exists;
        }
        if (g_registry.driver_count >= MAX_DRIVERS)
            return Status::NoSpace;
        g_registry.drivers[g_registry.driver_count++] = *__descriptor;
        return Status::Success;
    }

    Status publish(Device *__device, const void *__class_interface) noexcept {
        if (__device == nullptr || __class_interface == nullptr)
            return Status::InvalidArgument;
        __device->class_interface = __class_interface;
        __device->state = DeviceState::Published;
        __device->last_status = Status::Success;
        return Status::Success;
    }

    Status probe(DeviceClass __class, DeviceRole __role, ProbeContext __context) noexcept {
        if (!context_allows_probe(__context))
            return Status::UnsupportedContext;
        Device *device = find_mutable(__class, __role);
        if (device == nullptr)
            return Status::NotFound;
        if (device->state == DeviceState::Published)
            return Status::Success;

        const DriverDescriptor *driver = matching_driver(__class, __role);
        if (driver == nullptr)
            return Status::NotFound;

        device->state = DeviceState::Probing;
        device->driver = driver;
        device->class_interface = nullptr;
        const Status status = driver->probe(device);
        if (status != Status::Success) {
            device->state = DeviceState::ProbeFailed;
            device->last_status = status;
            device->class_interface = nullptr;
        }
        return status;
    }

    Status probe_all(ProbeContext __context) noexcept {
        if (!context_allows_probe(__context))
            return Status::UnsupportedContext;
        Status first_error = Status::Success;
        for (uint32_t i = 0; i < g_registry.device_count; i++) {
            Device &device = g_registry.devices[i];
            if (device.state == DeviceState::Published)
                continue;
            const Status status = probe(device.descriptor.device_class, device.descriptor.role, __context);
            if (status != Status::Success && first_error == Status::Success)
                first_error = status;
        }
        return first_error;
    }

    Status find(DeviceClass __class, DeviceRole __role, const Device **__out) noexcept {
        if (__out == nullptr)
            return Status::InvalidArgument;
        *__out = nullptr;
        Device *device = find_mutable(__class, __role);
        if (device == nullptr)
            return Status::NotFound;
        if (device->state != DeviceState::Published || device->class_interface == nullptr)
            return device->state == DeviceState::ProbeFailed ? Status::ProbeFailed : Status::NotReady;
        *__out = device;
        return Status::Success;
    }

    Status find_interface(DeviceClass __class, DeviceRole __role, const void **__out) noexcept {
        if (__out == nullptr)
            return Status::InvalidArgument;
        *__out = nullptr;
        const Device *device = nullptr;
        const Status status = find(__class, __role, &device);
        if (status != Status::Success)
            return status;
        *__out = device->class_interface;
        return Status::Success;
    }

    driver::block::BlockDevice *block(DeviceRole __role) noexcept {
        const void *iface = nullptr;
        if (find_interface(DeviceClass::Block, __role, &iface) != Status::Success)
            return nullptr;
        return (driver::block::BlockDevice *)iface;
    }

    const TimerInterface *timer(DeviceRole __role) noexcept {
        const void *iface = nullptr;
        if (find_interface(DeviceClass::Timer, __role, &iface) != Status::Success)
            return nullptr;
        return (const TimerInterface *)iface;
    }

    const VideoTextInterface *video_text(DeviceRole __role) noexcept {
        const void *iface = nullptr;
        if (find_interface(DeviceClass::Video, __role, &iface) != Status::Success)
            return nullptr;
        return (const VideoTextInterface *)iface;
    }

    const RtcInterface *rtc(DeviceRole __role) noexcept {
        const void *iface = nullptr;
        if (find_interface(DeviceClass::Rtc, __role, &iface) != Status::Success)
            return nullptr;
        return (const RtcInterface *)iface;
    }

    void init_pit_timer() noexcept {
        const TimerInterface *pit = timer(DeviceRole::PitTimer);
        if (pit != nullptr && pit->init_channel0 != nullptr) {
            pit->init_channel0();
            return;
        }
        driver::timer::pit::init_channel0();
    }

    void clear_video_text() noexcept {
        const VideoTextInterface *video = video_text(DeviceRole::VgaText);
        if (video != nullptr && video->clear_screen != nullptr) {
            video->clear_screen();
            return;
        }
        driver::video::vga::clear_screen();
    }

    void write_video_text(char __ch, uint8_t __color) noexcept {
        const VideoTextInterface *video = video_text(DeviceRole::VgaText);
        if (video != nullptr && video->write_char != nullptr) {
            video->write_char(__ch, __color);
            return;
        }
        driver::video::vga::write(__ch, __color);
    }

    void write_video_text(const char *__s, uint8_t __color) noexcept {
        const VideoTextInterface *video = video_text(DeviceRole::VgaText);
        if (video != nullptr && video->write_string != nullptr) {
            video->write_string(__s, __color);
            return;
        }
        driver::video::vga::write(__s, __color);
    }

    bool read_rtc_time(driver::rtc::DateTime *__out) noexcept {
        const RtcInterface *clock = rtc(DeviceRole::CmosRtc);
        if (clock != nullptr && clock->read_time != nullptr)
            return clock->read_time(__out);
        return false;
    }
}   // namespace device
NAMESPACE_BIGOS_END
