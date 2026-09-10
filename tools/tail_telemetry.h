#pragma once
#include <windows.h>

#include <setupapi.h>
#include <winioctl.h>
#include <batclass.h>
#include <powrprof.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <string>
#include <vector>

namespace pulse::tail {
inline constexpr double missing = std::numeric_limits<double>::quiet_NaN();
inline uint64_t ticks(FILETIME t) { return (uint64_t(t.dwHighDateTime) << 32) | t.dwLowDateTime; }
struct Handle {
    HANDLE value = nullptr;
    Handle() = default;
    explicit Handle(HANDLE h) : value(h) {}
    ~Handle() {
        if (value && value != INVALID_HANDLE_VALUE)
            CloseHandle(value);
    }
    Handle(const Handle &) = delete;
    Handle &operator=(const Handle &) = delete;
};
struct BatterySample {
    DWORD error = ERROR_NOT_FOUND, tag = 0, state = 0, capabilities = 0;
    LONG rawRate = static_cast<LONG>(BATTERY_UNKNOWN_RATE);
    ULONG rawCapacity = BATTERY_UNKNOWN_CAPACITY, rawVoltage = BATTERY_UNKNOWN_VOLTAGE;
    double dischargeW = missing, capacityWh = missing, voltageV = missing;
};
inline void convert(BatterySample &s) {
    s.dischargeW = s.capacityWh = s.voltageV = missing;
    if (s.error)
        return;
    const bool absolute = (s.capabilities & BATTERY_CAPACITY_RELATIVE) == 0;
    if (absolute && s.rawRate != static_cast<LONG>(BATTERY_UNKNOWN_RATE))
        s.dischargeW = -double(s.rawRate) / 1000.0;
    if (absolute && s.rawCapacity != BATTERY_UNKNOWN_CAPACITY)
        s.capacityWh = double(s.rawCapacity) / 1000.0;
    if (s.rawVoltage != BATTERY_UNKNOWN_VOLTAGE)
        s.voltageV = double(s.rawVoltage) / 1000.0;
}
class Battery {
    Handle device_;
    DWORD tag_ = 0;
    BATTERY_INFORMATION info_{};
    template <class In, class Out> DWORD query(DWORD code, In &in, Out &out) {
        DWORD returned = 0;
        if (!DeviceIoControl(device_.value, code, &in, sizeof(in), &out, sizeof(out), &returned,
                             nullptr))
            return GetLastError();
        return returned == sizeof(out) ? ERROR_SUCCESS : ERROR_INVALID_DATA;
    }
    DWORD refresh() {
        DWORD wait = 0;
        tag_ = 0;
        DWORD e = query(IOCTL_BATTERY_QUERY_TAG, wait, tag_);
        if (e || !tag_)
            return e ? e : ERROR_NOT_FOUND;
        BATTERY_QUERY_INFORMATION q{};
        q.BatteryTag = tag_;
        q.InformationLevel = BatteryInformation;
        info_ = {};
        return query(IOCTL_BATTERY_QUERY_INFORMATION, q, info_);
    }

  public:
    DWORD openError = ERROR_NOT_FOUND;
    Battery() {
        HDEVINFO devices = SetupDiGetClassDevsW(&GUID_DEVICE_BATTERY, nullptr, nullptr,
                                                DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
        if (devices == INVALID_HANDLE_VALUE) {
            openError = GetLastError();
            return;
        }
        for (DWORD i = 0; i < 32; ++i) {
            SP_DEVICE_INTERFACE_DATA item{};
            item.cbSize = sizeof(item);
            if (!SetupDiEnumDeviceInterfaces(devices, nullptr, &GUID_DEVICE_BATTERY, i, &item))
                break;
            DWORD needed = 0;
            SetupDiGetDeviceInterfaceDetailW(devices, &item, nullptr, 0, &needed, nullptr);
            if (!needed || needed > 65536)
                continue;
            std::vector<unsigned char> buffer(needed);
            auto detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W *>(buffer.data());
            detail->cbSize = sizeof(*detail);
            if (!SetupDiGetDeviceInterfaceDetailW(devices, &item, detail, needed, nullptr, nullptr))
                continue;
            device_.value =
                CreateFileW(detail->DevicePath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (device_.value == INVALID_HANDLE_VALUE) {
                openError = GetLastError();
                continue;
            }
            openError = refresh();
            if (!openError && (info_.Capabilities & BATTERY_SYSTEM_BATTERY) &&
                !(info_.Capabilities & BATTERY_IS_SHORT_TERM))
                break;
            CloseHandle(device_.value);
            device_.value = nullptr;
        }
        SetupDiDestroyDeviceInfoList(devices);
    }
    BatterySample read() {
        BatterySample s;
        if (!device_.value || device_.value == INVALID_HANDLE_VALUE) {
            s.error = openError ? openError : ERROR_NOT_FOUND;
            return s;
        }
        BATTERY_WAIT_STATUS request{};
        request.BatteryTag = tag_; // Timeout zero: request current status.
        BATTERY_STATUS result{};
        s.error = query(IOCTL_BATTERY_QUERY_STATUS, request, result);
        if (s.error == ERROR_FILE_NOT_FOUND || s.error == ERROR_NO_SUCH_DEVICE) {
            s.error = refresh();
            if (!s.error) {
                request.BatteryTag = tag_;
                s.error = query(IOCTL_BATTERY_QUERY_STATUS, request, result);
            }
        }
        s.tag = tag_;
        s.capabilities = info_.Capabilities;
        s.state = result.PowerState;
        s.rawRate = result.Rate;
        s.rawCapacity = result.Capacity;
        s.rawVoltage = result.Voltage;
        convert(s);
        return s;
    }
};

// Public HWiNFO shared-memory wire layout. Own bounded parser; no hardware access.
struct SensorValue {
    uint32_t sensorId = 0, instance = 0, readingId = 0, type = 0;
    std::string sensor, label, unit;
    double value = missing;
};
struct SensorSnapshot {
    int64_t pollTime = 0;
    uint32_t periodMs = 0;
    std::vector<SensorValue> values;
};
template <class T> inline T field(std::span<const unsigned char> data, size_t at) {
    T value{};
    std::memcpy(&value, data.data() + at, sizeof(T));
    return value;
}
inline std::string wireString(std::span<const unsigned char> data, size_t at, size_t size) {
    const auto *p = reinterpret_cast<const char *>(data.data() + at);
    size_t n = 0;
    while (n < size && p[n])
        ++n;
    return std::string(p, n);
}
inline bool parseSensors(std::span<const unsigned char> bytes, SensorSnapshot &out) {
    out = {};
    if (bytes.size() < 44 || field<uint32_t>(bytes, 0) != 0x53695748 ||
        field<uint32_t>(bytes, 4) != 1)
        return false;
    const auto rev = field<uint32_t>(bytes, 8);
    if (rev >= 1 && bytes.size() < 48)
        return false;
    const auto so = field<uint32_t>(bytes, 20), ss = field<uint32_t>(bytes, 24),
               sn = field<uint32_t>(bytes, 28);
    const auto ro = field<uint32_t>(bytes, 32), rs = field<uint32_t>(bytes, 36),
               rn = field<uint32_t>(bytes, 40);
    const size_t header = rev ? 48 : 44;
    auto section = [&](size_t offset, size_t stride, size_t count, size_t minimum) {
        return offset >= header && offset <= bytes.size() && stride >= minimum && count <= 8192 &&
               count <= (bytes.size() - offset) / stride;
    };
    if (!section(so, ss, sn, 264) || !section(ro, rs, rn, 316))
        return false;
    const size_t sensorEnd = size_t(so) + size_t(ss) * sn,
                 readingEnd = size_t(ro) + size_t(rs) * rn;
    if (sn && rn && !(sensorEnd <= ro || readingEnd <= so))
        return false;
    out.pollTime = field<int64_t>(bytes, 12);
    out.periodMs = rev ? field<uint32_t>(bytes, 44) : 0;
    out.values.reserve(rn);
    for (uint32_t i = 0; i < rn; ++i) {
        const size_t r = size_t(ro) + size_t(rs) * i;
        const auto sensor = field<uint32_t>(bytes, r + 4);
        if (sensor >= sn) {
            out = {};
            return false;
        }
        const size_t s = size_t(so) + size_t(ss) * sensor;
        SensorValue v;
        v.type = field<uint32_t>(bytes, r);
        v.readingId = field<uint32_t>(bytes, r + 8);
        v.sensorId = field<uint32_t>(bytes, s);
        v.instance = field<uint32_t>(bytes, s + 4);
        v.sensor = wireString(bytes, s + 8, 128);
        v.label = wireString(bytes, r + 12, 128);
        v.unit = wireString(bytes, r + 268, 16);
        v.value = field<double>(bytes, r + 284);
        out.values.push_back(std::move(v));
    }
    return true;
}
class Hwinfo {
    Handle mapping_, mutex_;
    const unsigned char *view_ = nullptr;
    size_t size_ = 0;
    std::vector<unsigned char> buffer_;

  public:
    DWORD error = ERROR_FILE_NOT_FOUND;
    Hwinfo() {
        mapping_.value = OpenFileMappingW(FILE_MAP_READ, FALSE, L"Global\\HWiNFO_SENS_SM2");
        if (!mapping_.value) {
            error = GetLastError();
            return;
        }
        mutex_.value =
            OpenMutexW(SYNCHRONIZE | MUTEX_MODIFY_STATE, FALSE, L"Global\\HWiNFO_SM2_MUTEX");
        if (!mutex_.value) {
            error = GetLastError();
            return;
        }
        view_ = static_cast<const unsigned char *>(
            MapViewOfFile(mapping_.value, FILE_MAP_READ, 0, 0, 0));
        if (!view_) {
            error = GetLastError();
            return;
        }
        MEMORY_BASIC_INFORMATION region{};
        if (!VirtualQuery(view_, &region, sizeof(region)) || region.State != MEM_COMMIT ||
            (region.Protect & (PAGE_GUARD | PAGE_NOACCESS)) ||
            region.RegionSize > 16 * 1024 * 1024) {
            error = ERROR_INVALID_DATA;
            return;
        }
        size_ = region.RegionSize;
        buffer_.resize(size_);
        error = 0;
    }
    ~Hwinfo() {
        if (view_)
            UnmapViewOfFile(view_);
    }
    bool read(SensorSnapshot &out) {
        out = {};
        if (!view_ || !size_)
            return false;
        const DWORD wait = WaitForSingleObject(mutex_.value, 0);
        if (wait != WAIT_OBJECT_0) {
            if (wait == WAIT_ABANDONED)
                ReleaseMutex(mutex_.value);
            error = wait == WAIT_TIMEOUT ? ERROR_BUSY : ERROR_INVALID_DATA;
            return false;
        }
        std::memcpy(buffer_.data(), view_, size_);
        ReleaseMutex(mutex_.value);
        const bool ok = parseSensors(buffer_, out);
        error = ok ? ERROR_SUCCESS : ERROR_INVALID_DATA;
        return ok;
    }
};
} // namespace pulse::tail
