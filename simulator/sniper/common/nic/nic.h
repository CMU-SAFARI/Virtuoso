// #pragma once
// #include <iostream>
// #include <vector>
// #include <queue>
// #include <cstdint>
// #include <cstring>
// #include <cassert>
// #include <functional>
// #include <chrono>
// #include <thread>
// #include <map>
// #include <string>
// #include "iio_pcie.h"
// #include "subsecond_time.h"

// // Constants for register addresses
// constexpr uint32_t REG_CTRL = 0x0000;
// constexpr uint32_t REG_STATUS = 0x0008;
// constexpr uint32_t REG_RCTL = 0x0100;
// constexpr uint32_t REG_TCTL = 0x0400;
// constexpr uint32_t REG_RDBAL = 0x2800;
// constexpr uint32_t REG_TDBAL = 0x3800;

// // Constants for timing
// constexpr uint64_t CLOCK_PERIOD = 100; // Simulated clock period in microseconds

// using namespace std;

// namespace NIC{
//     // Simplified packet structure
//     struct Packet {
//         std::string data;
//         uint32_t length;
//     };

//     // Basic Event class to simulate timing events
//     class Event {
//     public:
//         Event(uint64_t time, std::function<void()> action)
//             : time(time), action(action) {}

//         uint64_t getTime() const { return time; }
//         void execute() const { action(); }

//     private:
//         uint64_t time;
//         std::function<void()> action;
//     };

//     // BasicNIC class with timing model
//     class BasicNIC {
//     public:
//         BasicNIC();

//         void writeRegister(uint32_t addr, uint32_t value);
//         uint32_t readRegister(uint32_t addr);
        
//         void sendPacket(const Packet& pkt);
//         void receivePacket(Packet& pkt, SubsecondTime now);

//         void simulate();

//     private:
//         // Registers (simplified)
//         uint32_t ctrl;
//         uint32_t status;
//         uint32_t rctl;
//         uint32_t tctl;
//         uint32_t rdbal;
//         uint32_t tdbal;

//         // Simple FIFOs for TX and RX
//         std::queue<std::pair<Packet,SubsecondTime>> rxFifo;
//         int max_rxfifo_size;


//         std::queue<Packet> txFifo;
//         int max_txfifo_size;

//         struct RxDescriptor {
//             uint64_t virtual_page;
//         };

//         std::queue<RxDescriptor> rxDescriptors;
    

//         // Event queue for timing simulation
//         std::multimap<uint64_t, Event> eventQueue;

//         // Initialize a min-heap to figure out the times that a slot will be available in the rxfifo
//         class Compare
//         {
//         public:
//             bool operator()(SubsecondTime a, SubsecondTime b)
//             {
//                 return a > b;
//             }
//         };

//         std::priority_queue<SubsecondTime, std::vector<SubsecondTime>, Compare> slotReleaseTimes;


//         // Simulation clock
//         SubsecondTime currentTime;

//         PCIeDevice *pcie_protocol;

//         // Helper functions
//         void transmit();
//         void processReceivedPacket(Packet& pkt);

//         void scheduleEvent(uint64_t delay, std::function<void()> action);
//         void advanceTime();
//         void processEvents();

//         void cleanupReleasedSlots(SubsecondTime currentTime);
//         void cleanupRxFifo(SubsecondTime currentTime);
//     };

// }


