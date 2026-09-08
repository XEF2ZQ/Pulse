#include "demand_policy.h"
#include "process_discovery.h"
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace pulse;
static unsigned checks = 0;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        ++checks;                                                                                  \
        if (!(x))                                                                                  \
            throw std::runtime_error(#x);                                                          \
    } while (false)
int main() {
    try {
        DemandPolicy a, b;
        a.update(1000, 100, false, false, true);
        CHECK(!a.active);
        a.update(1250, 100, false, false, true);
        CHECK(!a.active);
        a.update(1500, 100, false, false, true);
        CHECK(a.active);
        for (uint64_t t = 1750; t < 601750; t += 250) {
            a.update(t, 100, false, false, true);
            CHECK(a.active);
        }
        a.update(601750, 0, false, false, true);
        a.update(602000, 0, false, false, true);
        CHECK(a.active);
        a.update(602250, 0, false, false, true);
        CHECK(!a.active);
        b.update(1000, 65, true, false, true);
        b.update(1250, 65, true, false, true);
        CHECK(b.active);
        b.update(1251, 100, true, false, false);
        CHECK(!b.active);
        a.reset();
        a.update(1000, 40, false, true, true);
        CHECK(a.active);
        a.update(1125, 0, false, false, true);
        CHECK(a.active);
        a.update(1250, 0, false, false, true);
        CHECK(!a.active);
        a.update(2000, 34, false, true, true);
        CHECK(!a.active);
        a.update(3000, 100, true, false, true);
        a.update(3250, 100, true, false, true);
        CHECK(a.active);
        a.update(3500, std::numeric_limits<double>::quiet_NaN(), true, false, true);
        CHECK(a.active);
        a.expire(4750);
        CHECK(!a.active);
        // Overlapping independent work must survive another job's completion.
        a.update(5000, 100, true, true, true);
        b.update(5000, 100, false, true, true);
        a.update(5001, 0, false, false, false);
        CHECK(!a.active && b.active);
        for (uint64_t t = 6000; t < 16000; t += 125) {
            a.update(t, 10, false, true, true);
            CHECK(!a.active);
        }
        std::vector<unsigned char> data(sizeof(SYSTEM_BASICPROCESS_INFORMATION) + 16);
        SYSTEM_BASICPROCESS_INFORMATION row{};
        row.UniqueProcessId = reinterpret_cast<HANDLE>(42);
        row.SequenceNumber = 7;
        row.ImageName.Buffer = reinterpret_cast<PWSTR>(data.data() + sizeof(row));
        row.ImageName.Length = 8;
        row.ImageName.MaximumLength = 8;
        std::memcpy(data.data(), &row, sizeof(row));
        std::memcpy(data.data() + sizeof(row), L"TEST", 8);
        std::vector<ProcessIdentity> result;
        CHECK(parseBasicProcesses(data, result));
        CHECK(result.size() == 1 && result[0].pid == 42 && result[0].name == L"test");
        for (size_t n = 0; n < sizeof(row) + 8; ++n)
            CHECK(!parseBasicProcesses(std::span(data).first(n), result));
        row.NextEntryOffset = 1;
        std::memcpy(data.data(), &row, sizeof(row));
        CHECK(!parseBasicProcesses(data, result));
        row.NextEntryOffset = 0;
        row.ImageName.Buffer = reinterpret_cast<PWSTR>(1);
        std::memcpy(data.data(), &row, sizeof(row));
        CHECK(!parseBasicProcesses(data, result));
        row.ImageName.Length = 3;
        std::memcpy(data.data(), &row, sizeof(row));
        CHECK(!parseBasicProcesses(data, result));
        std::cout << "PASS: " << checks << " productive demand and discovery checks\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
