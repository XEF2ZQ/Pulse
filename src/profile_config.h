#pragma once
#include <windows.h>
#include <array>
#include <string>

namespace pulse {
struct SourceProfile {
    bool parking = true, scaling = true;
    unsigned cores = 10, minimum = 15, hetero = 0, shortThreads = 5, longThreads = 5;
    bool valid() const {
        return cores <= 100 && minimum <= 100 && hetero <= 4 && shortThreads <= 5 &&
               longThreads <= 5;
    }
    unsigned coreValue() const { return parking ? cores : 100; }
    unsigned minimumValue() const { return scaling ? minimum : 100; }
    bool operator==(const SourceProfile &) const = default;
};
struct Profiles {
    SourceProfile ac{true, true, 15, 80, 0, 5, 5}, dc{};
    bool valid() const { return ac.valid() && dc.valid(); }
    bool operator==(const Profiles &) const = default;
    DWORD value(unsigned setting, bool plugged) const {
        const auto &p = plugged ? ac : dc;
        switch (setting) {
        case 1:
        case 2:
            return p.coreValue();
        case 3:
        case 4:
            return p.minimumValue();
        case 5:
            return p.hetero;
        case 6:
            return p.longThreads;
        case 7:
            return p.shortThreads;
        default:
            return 0;
        }
    }
};
inline bool parsePercent(const std::wstring &text, unsigned &out) {
    if (text.empty() || text.size() > 3)
        return false;
    unsigned value = 0;
    for (wchar_t c : text) {
        if (c < L'0' || c > L'9')
            return false;
        value = value * 10 + unsigned(c - L'0');
    }
    if (value > 100)
        return false;
    out = value;
    return true;
}
// The INI stores drafts/defaults, never proof that hardware accepted a setting.
inline Profiles loadProfiles(const std::wstring &path) {
    Profiles result;
    for (bool ac : {false, true}) {
        auto &p = ac ? result.ac : result.dc;
        const wchar_t *section = ac ? L"PowerAC" : L"PowerDC";
        p.parking = GetPrivateProfileIntW(section, L"Parking", 1, path.c_str()) != 0;
        p.scaling = GetPrivateProfileIntW(section, L"Scaling", 1, path.c_str()) != 0;
        p.cores = GetPrivateProfileIntW(section, L"Cores", p.cores, path.c_str());
        p.minimum = GetPrivateProfileIntW(section, L"Minimum", p.minimum, path.c_str());
        p.hetero = GetPrivateProfileIntW(section, L"Hetero", 0, path.c_str());
        p.shortThreads = GetPrivateProfileIntW(section, L"ShortThreads", 5, path.c_str());
        p.longThreads = GetPrivateProfileIntW(section, L"LongThreads", 5, path.c_str());
        if (!p.valid())
            p = ac ? Profiles{}.ac : Profiles{}.dc;
    }
    return result;
}
inline bool saveProfiles(const std::wstring &path, const Profiles &profiles) {
    if (!profiles.valid())
        return false;
    const auto temp = path + L".profiles.tmp";
    if (!CopyFileW(path.c_str(), temp.c_str(), FALSE)) {
        if (GetLastError() != ERROR_FILE_NOT_FOUND)
            return false;
        HANDLE fresh = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                   FILE_ATTRIBUTE_NORMAL, nullptr);
        if (fresh == INVALID_HANDLE_VALUE)
            return false;
        CloseHandle(fresh);
    }
    for (bool ac : {false, true}) {
        const auto &p = ac ? profiles.ac : profiles.dc;
        const wchar_t *section = ac ? L"PowerAC" : L"PowerDC";
        const std::array<std::pair<const wchar_t *, unsigned>, 7> values{
            {{L"Parking", p.parking},
             {L"Scaling", p.scaling},
             {L"Cores", p.cores},
             {L"Minimum", p.minimum},
             {L"Hetero", p.hetero},
             {L"ShortThreads", p.shortThreads},
             {L"LongThreads", p.longThreads}}};
        for (const auto &[key, value] : values)
            if (!WritePrivateProfileStringW(section, key, std::to_wstring(value).c_str(),
                                            temp.c_str()))
                return false;
    }
    WritePrivateProfileStringW(nullptr, nullptr, nullptr, temp.c_str());
    if (!MoveFileExW(temp.c_str(), path.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        return false;
    WritePrivateProfileStringW(nullptr, nullptr, nullptr, path.c_str());
    return true;
}
} // namespace pulse
