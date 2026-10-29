// Explicit manual policy transaction test. Never runs at normal startup.
static int idleIntervalComparison(const std::wstring &output) {
    SYSTEM_POWER_STATUS source{};
    if(!GetSystemPowerStatus(&source)||source.ACLineStatus!=0)return ERROR_NOT_READY;
    std::wostringstream report;report<<L"pair,enabled,state,policy_ms\n";
    DWORD error=guardian();if(!error)error=power.restore();
    const auto previous=power.cpuOptions;
    for(unsigned pair=0;pair<4&&!error;++pair) for(unsigned arm=0;arm<2&&!error;++arm) {
        power.cpuOptions=cpu::Options{};
        power.cpuOptions.earlyIdle=(arm==(pair%2));
        power.cpuOptions.idleUs=100;
        error=power.capture(2);
        if(!error)error=power.configure();
        for(auto state:{PowerState::Burst,PowerState::Compute,PowerState::Efficiency}) {
            if(error)break;
            if(!GetSystemPowerStatus(&source)||source.ACLineStatus!=0){error=ERROR_NOT_READY;break;}
            error=power.apply(state);
            report<<pair<<L','<<power.cpuOptions.earlyIdle<<L','<<static_cast<int>(state)<<L','<<power.lastApplyMs<<L'\n';
        }
        const DWORD restored=power.restore();if(!error)error=restored;
    }
    power.cpuOptions=previous;
    report<<L"error="<<error<<L"; journal="<<power.hasJournal()<<L'\n';
    if(!saveText(output,report.str()))return ERROR_WRITE_FAULT;
    return static_cast<int>(error);
}
static int idleIntervalExercise(const std::wstring &output, bool crash) {
    std::wostringstream report;
    report << L"Idle interval transaction; no physical C-state claim\n";
    SYSTEM_POWER_STATUS source{};
    if (!GetSystemPowerStatus(&source) || source.ACLineStatus != 0)
        return ERROR_NOT_READY;
    DWORD error=guardian();
    if (!error) error=power.restore();
    if (!error && !power.optionAvailable(13,2)) error=ERROR_NOT_SUPPORTED;
    const auto previous=power.cpuOptions;
    power.cpuOptions=cpu::Options{};
    power.cpuOptions.earlyIdle=true;
    power.cpuOptions.idleUs=100;
    if (!error) error=power.capture(2);
    const auto snapshot=power.saved;
    if (!error) error=power.configure();
    if (!error && crash) {
        report << L"Armed v5 recovery with DC interval 100 us. Parent exits without restore.\n";
        if (!saveText(output,report.str())) error=ERROR_WRITE_FAULT;
        else ExitProcess(77); // Explicit test only; existing guardian restores.
    }
    for (auto state : {PowerState::Efficiency,PowerState::Burst,PowerState::Compute,
                       PowerState::Efficiency}) {
        if (error) break;
        if (!GetSystemPowerStatus(&source)||source.ACLineStatus!=0) {error=ERROR_NOT_READY;break;}
        error=power.apply(state);
        if (!error) error=power.verify(state,true);
        DWORD ac=0,dc=0;
        if (!error) error=power.read(snapshot.scheme,13,true,ac);
        if (!error) error=power.read(snapshot.scheme,13,false,dc);
        bool found=false;
        for (DWORD i=0;i<snapshot.count&&!error;++i) {
            const auto &r=snapshot.rows[i];
            if (r.setting!=13) continue;
            found=true;
            const DWORD expected=state==PowerState::Efficiency?std::min<DWORD>(r.dc,100):r.dc;
            if (r.mask!=2||ac!=r.ac||dc!=expected) error=ERROR_INVALID_STATE;
        }
        if (!error&&!found) error=ERROR_NOT_FOUND;
        report << L"state=" << static_cast<int>(state) << L" AC=" << ac << L" DC=" << dc
               << L" policy-apply-ms=" << power.lastApplyMs << L" error=" << error << L'\n';
        const auto writes=power.writes, commits=power.planCommits;
        double repeatedTotal=0;
        for(unsigned repeat=0;repeat<3&&!error;++repeat){
            error=power.apply(state);repeatedTotal+=power.lastApplyMs;
        }
        if(!error&&(power.writes!=writes||power.planCommits!=commits))error=ERROR_INVALID_STATE;
        report<<L"Repeated unchanged requests: writes="<<power.writes-writes
              <<L" commits="<<power.planCommits-commits<<L" mean-policy-ms="<<repeatedTotal/3
              <<L" error="<<error<<L'\n';
    }
    const DWORD restored=power.restore();if (!error) error=restored;
    for (DWORD i=0;i<snapshot.count&&!error;++i) {
        const auto &r=snapshot.rows[i];
        for (bool ac : {false,true}) {
            DWORD value=0;error=power.read(r.scheme,r.setting,ac,value);
            if (!error&&value!=(ac?r.ac:r.dc)) error=ERROR_INVALID_STATE;
            if(error)break;
        }
    }
    report << L"Exact captured values restored: " << error << L"; journal=" << power.hasJournal() << L'\n';
    // No ownership on real AC or on a weak-charger AC-profile bridge.
    for (bool bridge : {false,true}) {
        if (error) break;
        power.dcUsesAcProfile=bridge;power.acPassthrough=bridge;
        error=power.capture(bridge?2:1);
        if (!error) for (DWORD i=0;i<power.saved.count;++i)
            if(power.saved.rows[i].setting==13) error=ERROR_INVALID_STATE;
        const DWORD e=power.restore();if(!error)error=e;
        report << (bridge?L"Weak-charger exclusion: ":L"AC exclusion: ") << error << L'\n';
    }
    power.dcUsesAcProfile=false;power.acPassthrough=false;
    power.cpuOptions=previous;
    report << L"Final error=" << error << L"; journal=" << power.hasJournal() << L'\n';
    if (!saveText(output,report.str())) return ERROR_WRITE_FAULT;
    return static_cast<int>(error);
}
