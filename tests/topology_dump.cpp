#include "scheduler_topology.h"
#include <fstream>
#include <filesystem>
#include <iostream>
int wmain(int argc,wchar_t** argv) {
    const auto topology=pulse::scheduler::discoverTopology();
    if(argc==2){std::ofstream out(std::filesystem::path(argv[1]),std::ios::binary);out<<topology.json();if(!out)return 2;}
    else std::cout<<topology.json();
    return topology.valid()?0:1;
}
