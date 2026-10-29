#include <initguid.h>
#include "../tools/tail_telemetry.h"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <stdexcept>
using namespace pulse::tail;
static unsigned checks = 0;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        ++checks;                                                                                  \
        if (!(x))                                                                                  \
            throw std::runtime_error(#x);                                                          \
    } while (false)
template <class T> void put(std::vector<unsigned char> &b, size_t at, T value) {
    std::memcpy(b.data() + at, &value, sizeof(value));
}
int wmain(int argc, wchar_t **argv) {
    try {
        if (argc > 2) throw std::runtime_error("Usage: tail_tests [captured-shared-memory.bin]");
        BatterySample b;
        b.error = 0;
        b.rawRate = -18000;
        b.rawCapacity = 55000;
        b.rawVoltage = 16000;
        convert(b);
        CHECK(b.dischargeW == 18 && b.capacityWh == 55 && b.voltageV == 16);
        b.rawRate = 9000;
        convert(b);
        CHECK(b.dischargeW == -9); // Charge is signed, never silently labelled discharge.
        b.rawRate = static_cast<LONG>(BATTERY_UNKNOWN_RATE);
        convert(b);
        CHECK(std::isnan(b.dischargeW));
        b.rawRate = -18000;
        b.capabilities = BATTERY_CAPACITY_RELATIVE;
        convert(b);
        CHECK(std::isnan(b.dischargeW) && std::isnan(b.capacityWh));
        b.capabilities = 0;
        b.error = ERROR_DEVICE_NOT_CONNECTED;
        convert(b);
        CHECK(std::isnan(b.dischargeW) && std::isnan(b.voltageV));
        b.error = 0;
        b.rawVoltage = BATTERY_UNKNOWN_VOLTAGE;
        b.rawCapacity = BATTERY_UNKNOWN_CAPACITY;
        convert(b);
        CHECK(std::isnan(b.voltageV) && std::isnan(b.capacityWh));
        std::vector<unsigned char> bytes(48 + 264 + 316, 0);
        put<uint32_t>(bytes, 0, 0x53695748);
        put<uint32_t>(bytes, 4, 1);
        put<uint32_t>(bytes, 8, 1);
        put<int64_t>(bytes, 12, 12345);
        put<uint32_t>(bytes, 20, 48);
        put<uint32_t>(bytes, 24, 264);
        put<uint32_t>(bytes, 28, 1);
        put<uint32_t>(bytes, 32, 312);
        put<uint32_t>(bytes, 36, 316);
        put<uint32_t>(bytes, 40, 1);
        put<uint32_t>(bytes, 44, 2000);
        put<uint32_t>(bytes, 48, 42);
        put<uint32_t>(bytes, 52, 2);
        std::memcpy(bytes.data() + 56, "CPU", 4);
        put<uint32_t>(bytes, 312, 5);
        put<uint32_t>(bytes, 320, 99);
        std::memcpy(bytes.data() + 324, "CPU Package Power", 18);
        std::memcpy(bytes.data() + 580, "W", 2);
        put<double>(bytes, 596, 4.5);
        SensorSnapshot s;
        CHECK(parseSensors(bytes, s));
        CHECK(s.pollTime == 12345 && s.periodMs == 2000 && s.values.size() == 1);
        CHECK(s.values[0].value == 4.5 && s.values[0].label == "CPU Package Power" &&
              s.values[0].sensorId == 42 && s.values[0].instance == 2);
        for (size_t n = 0; n < bytes.size(); ++n)
            CHECK(!parseSensors(std::span(bytes).first(n), s));
        auto bad = bytes;
        put<uint32_t>(bad, 316, 1);
        CHECK(!parseSensors(bad, s) && s.values.empty());
        bad = bytes;
        put<uint32_t>(bad, 28, UINT32_MAX);
        CHECK(!parseSensors(bad, s));
        bad = bytes;
        put<uint32_t>(bad, 24, UINT32_MAX);
        CHECK(!parseSensors(bad, s));
        bad = bytes;
        put<uint32_t>(bad, 32, 48);
        CHECK(!parseSensors(bad, s));
        bad = bytes;
        put<uint32_t>(bad, 4, 99);
        CHECK(!parseSensors(bad, s));
        bad = bytes;
        put<uint32_t>(bad, 0, 0);
        CHECK(!parseSensors(bad, s));
        bad = bytes;
        std::fill(bad.begin() + 324, bad.begin() + 452, 'X');
        CHECK(parseSensors(bad, s) && s.values[0].label.size() == 128);
        // Replay the observed v2 record sizes. Numeric values are still in the
        // original prefix; extension bytes must never be mistaken for doubles.
        std::vector<unsigned char> v2(48 + 392 + 460, 0xA5);
        std::copy_n(bytes.begin(), 48, v2.begin());
        std::copy_n(bytes.begin() + 48, 264, v2.begin() + 48);
        std::copy_n(bytes.begin() + 312, 316, v2.begin() + 440);
        put<uint32_t>(v2, 4, 2);
        put<uint32_t>(v2, 24, 392);
        put<uint32_t>(v2, 32, 440);
        put<uint32_t>(v2, 36, 460);
        CHECK(parseSensors(v2, s));
        CHECK(snapshotSize(std::span(v2).first(48), 20029440) == v2.size());
        CHECK(snapshotSize(std::span(v2).first(48), v2.size() - 1) == 0);
        CHECK(snapshotSize(std::span(v2).first(44), v2.size()) == 0);
        CHECK(s.version == 2 && s.revision == 1 && s.periodMs == 2000);
        CHECK(s.values.size() == 1 && s.values[0].value == 4.5 &&
              s.values[0].unit == "W" && s.values[0].label == "CPU Package Power");
        for (size_t n = 0; n < v2.size(); ++n)
            CHECK(!parseSensors(std::span(v2).first(n), s) && s.values.empty());
        bad = v2;
        put<uint32_t>(bad, 24, 264);
        CHECK(!parseSensors(bad, s));
        bad = v2;
        put<uint32_t>(bad, 36, 316);
        CHECK(!parseSensors(bad, s));
        bad = v2;
        put<uint32_t>(bad, 8, 2);
        CHECK(!parseSensors(bad, s));
        bad = v2;
        put<uint32_t>(bad, 4, 3);
        CHECK(!parseSensors(bad, s));
        bad = v2;
        put<uint32_t>(bad, 32, UINT32_MAX);
        CHECK(snapshotSize(bad, SIZE_MAX) == 0);
        bad = v2;
        put<uint32_t>(bad, 36, UINT32_MAX);
        CHECK(snapshotSize(bad, SIZE_MAX) == 0);
        bad = v2;
        put<uint32_t>(bad, 36, 0);
        CHECK(snapshotSize(bad, SIZE_MAX) == 0);
        if (argc == 2) {
            // Offline replay of a locally captured, mutex-protected snapshot.
            const auto path = std::filesystem::path(argv[1]);
            const auto length = std::filesystem::file_size(path);
            CHECK(length >= 48 && length <= 64 * 1024 * 1024);
            std::vector<unsigned char> captured(static_cast<size_t>(length));
            std::ifstream input(path, std::ios::binary);
            CHECK(bool(input.read(reinterpret_cast<char *>(captured.data()),
                                  static_cast<std::streamsize>(length))));
            const size_t used = snapshotSize(captured, captured.size());
            CHECK(used != 0);
            CHECK(parseSensors(std::span(captured).first(used), s));
            CHECK(s.values.size() == field<uint32_t>(captured, 40));
            std::cout << "REPLAY: version=" << s.version << " revision=" << s.revision
                      << " readings=" << s.values.size() << " copied_bytes=" << used
                      << " reserved_bytes=" << length << '\n';
        }
        std::cout << "PASS: " << checks
                  << " battery conversion and shared-memory boundary checks\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
