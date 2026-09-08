#pragma once
#include <windows.h> // Must precede the dependent Windows SDK headers below.

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <cwctype>
#include <span>
#include <string>
#include <tlhelp32.h>
#include <vector>
#include <winternl.h>
namespace pulse {
struct ProcessIdentity {
    DWORD pid = 0, parent = 0;
    uint64_t sequence = 0;
    std::wstring name;
};
inline bool parseBasicProcesses(std::span<const unsigned char> bytes,
                                std::vector<ProcessIdentity> &out) {
    out.clear();
    size_t offset = 0;
    while (offset < bytes.size()) {
        if (bytes.size() - offset < sizeof(SYSTEM_BASICPROCESS_INFORMATION))
            return false;
        SYSTEM_BASICPROCESS_INFORMATION row{};
        std::memcpy(&row, bytes.data() + offset, sizeof(row));
        const size_t length = row.NextEntryOffset ? row.NextEntryOffset : bytes.size() - offset;
        if (length < sizeof(row) || length > bytes.size() - offset ||
            (row.NextEntryOffset && row.NextEntryOffset % alignof(void *) != 0))
            return false;
        const auto start = reinterpret_cast<uintptr_t>(bytes.data()),
                   pointer = reinterpret_cast<uintptr_t>(row.ImageName.Buffer);
        if (row.ImageName.Length % sizeof(wchar_t) ||
            row.ImageName.Length > row.ImageName.MaximumLength)
            return false;
        if (row.ImageName.Length &&
            (pointer < start + offset || pointer - start - offset > length ||
             row.ImageName.Length > length - (pointer - start - offset)))
            return false;
        const auto pid = reinterpret_cast<uintptr_t>(row.UniqueProcessId),
                   parent = reinterpret_cast<uintptr_t>(row.InheritedFromUniqueProcessId);
        if (pid > MAXDWORD || parent > MAXDWORD || out.size() >= 32768)
            return false;
        ProcessIdentity entry{
            static_cast<DWORD>(pid), static_cast<DWORD>(parent), row.SequenceNumber, {}};
        if (row.ImageName.Length) {
            entry.name.resize(row.ImageName.Length / sizeof(wchar_t));
            std::memcpy(entry.name.data(), row.ImageName.Buffer, row.ImageName.Length);
        }
        std::transform(entry.name.begin(), entry.name.end(), entry.name.begin(),
                       [](wchar_t c) { return wchar_t(towlower(c)); });
        out.push_back(std::move(entry));
        if (!row.NextEntryOffset)
            return true;
        offset += row.NextEntryOffset;
    }
    return false;
}
class ProcessDiscovery {
    using Query = NTSTATUS(NTAPI *)(SYSTEM_INFORMATION_CLASS, PVOID, ULONG, PULONG);
    Query query_ = reinterpret_cast<Query>(
        GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQuerySystemInformation"));
    bool native_ = true;
    std::vector<unsigned char> buffer_;

  public:
    uint64_t nativeCalls = 0, fallbackCalls = 0, failures = 0;
    bool collect(std::vector<ProcessIdentity> &out) {
        if (native_ && query_) {
            if (buffer_.empty())
                buffer_.resize(65536);
            for (int attempt = 0; attempt < 5; ++attempt) {
                ULONG required = 0;
                ++nativeCalls;
                const NTSTATUS status = query_(SystemBasicProcessInformation, buffer_.data(),
                                               static_cast<ULONG>(buffer_.size()), &required);
                if (status >= 0) {
                    if (!required || required > buffer_.size() ||
                        !parseBasicProcesses(std::span(buffer_).first(required), out)) {
                        ++failures;
                        break;
                    }
                    return true;
                }
                if (status == static_cast<NTSTATUS>(0xC0000004L) ||
                    status == static_cast<NTSTATUS>(0xC0000023L)) {
                    size_t next = std::max(size_t(required), buffer_.size() * 2);
                    if (next > 16 * 1024 * 1024) {
                        ++failures;
                        break;
                    }
                    buffer_.resize(next);
                    continue;
                }
                native_ = false;
                break;
            }
        }
        out.clear();
        ++fallbackCalls;
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE) {
            ++failures;
            return false;
        }
        PROCESSENTRY32W p{};
        p.dwSize = sizeof(p);
        bool ok = Process32FirstW(snapshot, &p) != FALSE;
        if (ok)
            do {
                ProcessIdentity entry{p.th32ProcessID, p.th32ParentProcessID, 0, p.szExeFile};
                std::transform(entry.name.begin(), entry.name.end(), entry.name.begin(),
                               [](wchar_t c) { return wchar_t(towlower(c)); });
                out.push_back(std::move(entry));
            } while (Process32NextW(snapshot, &p));
        CloseHandle(snapshot);
        if (!ok)
            ++failures;
        return ok;
    }
};
} // namespace pulse
