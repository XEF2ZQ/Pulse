#include "profile_config.h"
#include "placement_policy.h"
#include "undervolt.h"
#include <iostream>
#include <stdexcept>
static int checks = 0;
static void check(bool b, const char *text) {
    ++checks;
    if (!b)
        throw std::runtime_error(text);
}
int main() {
    try {
        using namespace pulse;
        Profiles p;
        check(p.valid(), "valid defaults");
        check(p.value(1, false) == 10 && p.value(3, false) == 15 && p.value(1, true) == 15 &&
                  p.value(3, true) == 80,
              "source mappings");
        p.dc.parking = false;
        p.ac.scaling = false;
        check(p.value(1, false) == 100 && p.value(2, false) == 100 && p.value(3, true) == 100 &&
                  p.value(4, true) == 100,
              "off means 100 percent");
        for (unsigned i = 0; i <= 100; ++i) {
            p.dc.cores = i;
            p.dc.minimum = i;
            check(p.valid(), "all percentages");
        }
        p.dc.cores = 101;
        check(!p.valid(), "reject percentage overflow");
        p = Profiles{};
        p.dc.hetero = 5;
        check(!p.valid(), "reject hetero overflow");
        unsigned percent = 0;
        for (const auto *s : {L"", L"-1", L"101", L"1.0", L"25%", L" 25", L"1x"})
            check(!parsePercent(s, percent), "reject malformed percentage");
        check(parsePercent(L"100", percent) && percent == 100, "parse endpoint");
        int offset = 0;
        for (const auto *s : {L"", L"-", L"-251", L"1", L"-5mV", L"-0.1"})
            check(!undervolt::parseOffset(s, offset), "reject malformed offset");
        check(undervolt::parseOffset(L"-50", offset) && offset == -50, "CO draft input");
        undervolt::Capability c;
        c.writer = true;
        c.independentDomains = true;
        c.readback = true;
        check(!c.canApply(), "unknown lock blocks writes");
        c.lock = undervolt::LockStatus::Unlocked;
        c.readback = false;
        check(!c.canMonitor(), "missing readback blocks monitoring");
        using placement::Evidence;
        using placement::Policy;
        Policy a;
        Evidence light{true, false, true, 10};
        check(a.update(1000, light) == Policy::Action::None, "first candidate");
        check(a.update(5999, light) == Policy::Action::None, "minimum evidence");
        check(a.update(6000, light) == Policy::Action::SelectEfficient,
              "stable background can select");
        a.committed(true);
        check(a.update(7000, {true, false, true, 49}) == Policy::Action::None, "hysteresis");
        check(a.update(8000, {true, false, true, 50}) == Policy::Action::Release,
              "pressure releases immediately");
        a.committed(false);
        a.update(9000, light);
        check(a.update(14000, light) == Policy::Action::None,
              "cooldown prevents migration chatter");
        check(a.update(23000, light) == Policy::Action::SelectEfficient, "retry after cooldown");
        a.committed(true);
        check(a.update(24000, {false, false, true, 10}) == Policy::Action::Release,
              "foreground/ineligible release");
        check(a.update(24000, {true, true, true, 10}) == Policy::Action::Release,
              "productive work release");
        check(a.update(24000, {true, false, false, 10}) == Policy::Action::Release,
              "missing evidence release");
        Policy idle;
        idle.update(1000, {true, false, true, 0});
        check(idle.update(30000, {true, false, true, 0}) == Policy::Action::None,
              "leave sleeping threads alone");
        std::wcout << checks << L" controls and placement policy checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
