//Write a program that has a main function and uses the  SimSendToNic(pkt) function 

#include <iostream>
#include <vector>
#include <queue>
#include <cstdint>
#include <cstring>
#include <cassert>
#include "sim_api.h"

int main() {
    

    char* pkt = new char[10];

    strcpy(pkt, "Hello");

    pkt[5] = '\0';

    SimSendToNic((long unsigned int)pkt, 10);

    return 0;
}
