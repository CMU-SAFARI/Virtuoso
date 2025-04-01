#include <iostream>
#include <vector>
#include <queue>
#include <cstdint>
#include <cstring>
#include "sim_api.h"



int main() {

    char* pkt = new char[10];

    strcpy(pkt, "Hello");

    SimReceiveFromNic((long unsigned int)pkt);
    
    return 0;
}