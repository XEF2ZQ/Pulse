#pragma once
#include <windows.h>
#include <cstdint>
#include <compare>
#include <optional>
#include <string>
#include <vector>
#include <span>

namespace pulse::scheduler {
struct CpuKey {
    WORD group = 0;
    BYTE index = 0;
    auto operator<=>(const CpuKey&) const = default;
};
struct Domain {
    unsigned id = 0;
    LOGICAL_PROCESSOR_RELATIONSHIP relationship{};
    unsigned efficiency = 0, flags = 0, level = 0, type = 0, bytes = 0, lineBytes = 0;
    std::vector<CpuKey> cpus;
};
struct LogicalCpu {
    CpuKey key;
    unsigned core = 0;
    std::optional<unsigned> package, die, module, numa, llc;
    std::vector<unsigned> caches;
    std::vector<CpuKey> siblings;
    std::optional<unsigned> cpuSetId, setCore, setLlc, setNuma, efficiency, schedulingClass;
    unsigned cpuSetFlags = 0;
};
struct CpuCapabilities {
    bool topologyValid = false, hasCpuSets = false, hasSmt = false, hasQoSApi = false;
    bool heterogeneous = false;
    unsigned efficiencyClassCount = 0, schedulingClassCount = 0, cacheDomainCount = 0;
};
struct Topology {
    std::string vendor;
    unsigned family = 0, model = 0;
    std::vector<Domain> domains;
    std::vector<LogicalCpu> cpus;
    std::vector<std::string> errors, notes;
    CpuCapabilities capabilities;
    unsigned activeCpuCount = 0;
    uint64_t fingerprint = 0;
    double discoveryMs = 0;
    bool valid() const { return capabilities.topologyValid; }
    std::string json() const;
};
// Parsers reject malformed lengths and unknown records are skipped by Size.
Topology parseTopology(std::span<const unsigned char> relations,
    std::span<const unsigned char> cpuSets, unsigned activeCpuCount);
Topology discoverTopology(); // Read only; called at startup/resume/device change.
}
