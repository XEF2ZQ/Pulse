#include <initguid.h>
#include "tail_telemetry.h"
#include <iostream>

int main() {
    SYSTEM_POWER_STATUS source{};
    if(GetSystemPowerStatus(&source))std::cout<<"Windows ACLineStatus="<<unsigned(source.ACLineStatus)<<" battery_flag="<<unsigned(source.BatteryFlag)<<"\n";
    pulse::tail::Battery battery;
    const auto b=battery.read();
    std::cout<<"Battery error="<<b.error<<" state="<<b.state<<" raw_rate="<<b.rawRate<<"\n";
    // Fixed read-only ASUS DSTS query. No INIT, DEVS, or UCSI commands.
    HANDLE handle=CreateFileW(L"\\\\.\\ATKACPI",0,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr);
    if(handle==INVALID_HANDLE_VALUE){std::cout<<"ASUS open error="<<GetLastError()<<"\n";return 0;}
    DWORD request[4]{0x53545344,8,0x0012006c,0};
    DWORD response[4]{};DWORD returned=0;
    if(DeviceIoControl(handle,0x0022240c,request,sizeof(request),response,sizeof(response),&returned,nullptr))
        std::cout<<"ASUS charger raw="<<response[0]<<" bytes="<<returned<<" presence="<<bool(response[0]&0x10000)<<" mode="<<(response[0]&0xffff)<<"\n";
    else std::cout<<"ASUS read error="<<GetLastError()<<"\n";
    CloseHandle(handle);return 0;
}
