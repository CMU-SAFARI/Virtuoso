

// #include <iostream>
// #include <queue>
// #include <functional>
// #include <thread>
// #include <map>
// #include "nic.h"
// #include "subsecond_time.h"


// namespace NIC {

//     BasicNIC::BasicNIC()
//         : ctrl(0), status(0), rctl(1), tctl(0), rdbal(0), tdbal(0), currentTime(SubsecondTime::Zero()), max_rxfifo_size(16), max_txfifo_size(16) {

//          pcie_protocol = new PCIeDevice();
//         }

//     void BasicNIC::writeRegister(uint32_t addr, uint32_t value) {

//             switch (addr) {
//                 case REG_CTRL:
//                     ctrl = value;
//                     std::cout << "Write to CTRL: " << std::hex << value << std::endl;
//                     break;
//                 case REG_STATUS:
//                     status = value;
//                     std::cout << "Write to STATUS: " << std::hex << value << std::endl;
//                     break;
//                 case REG_RCTL:
//                     rctl = value;
//                     std::cout << "Write to RCTL: " << std::hex << value << std::endl;
//                     break;
//                 case REG_TCTL:
//                     tctl = value;
//                     std::cout << "Write to TCTL: " << std::hex << value << std::endl;
//                     break;
//                 case REG_RDBAL:
//                     rdbal = value;
//                     std::cout << "Write to RDBAL: " << std::hex << value << std::endl;
//                     break;
//                 case REG_TDBAL:
//                     tdbal = value;
//                     std::cout << "Write to TDBAL: " << std::hex << value << std::endl;
//                     break;
//                 default:
//                     std::cerr << "Write to unknown register: " << std::hex << addr << std::endl;
//                     break;
//             };
//     }

//     uint32_t BasicNIC::readRegister(uint32_t addr) {
//         uint32_t result = 0;
//             switch (addr) {
//                 case REG_CTRL:
//                     result = ctrl;
//                     std::cout << "Read from CTRL: " << std::hex << ctrl << std::endl;
//                     break;
//                 case REG_STATUS:
//                     result = status;
//                     std::cout << "Read from STATUS: " << std::hex << status << std::endl;
//                     break;
//                 case REG_RCTL:
//                     result = rctl;
//                     std::cout << "Read from RCTL: " << std::hex << rctl << std::endl;
//                     break;
//                 case REG_TCTL:
//                     result = tctl;
//                     std::cout << "Read from TCTL: " << std::hex << tctl << std::endl;
//                     break;
//                 case REG_RDBAL:
//                     result = rdbal;
//                     std::cout << "Read from RDBAL: " << std::hex << rdbal << std::endl;
//                     break;
//                 case REG_TDBAL:
//                     result = tdbal;
//                     std::cout << "Read from TDBAL: " << std::hex << tdbal << std::endl;
//                     break;
//                 default:
//                     std::cerr << "Read from unknown register: " << std::hex << addr << std::endl;
//                     result = 0;
//                     break;
//             };
//         return result;
//     }

//     void BasicNIC::sendPacket(const Packet& pkt) {
//         if (tctl & 0x1) { // Check if TX is enabled
//             scheduleEvent(5, [this, pkt]() {
//                 txFifo.push(pkt);
//                 std::cout << "Packet enqueued for transmission." << std::endl;
//                 transmit();
//             });
//         } else {
//             std::cerr << "TX is disabled. Packet dropped." << std::endl;
//         }
//     }

//     void  BasicNIC::receivePacket(Packet& pkt, SubsecondTime currentTime) {

//         std::cout << "[NIC] Try to insert packet into RX FIFO." << std::endl;

//         cleanupReleasedSlots(currentTime);
//         cleanupRxFifo(currentTime);

//         if (rctl & 0x1) { // Check if RX is enabled

//             SubsecondTime stall_time = SubsecondTime::Zero();

//             std::cout << "[NIC] RX FIFO size: " << rxFifo.size() << std::endl;
            
//             //check if rxfifo is full
//            if (rxFifo.size() >= max_rxfifo_size) {
//                 // If RX FIFO is full, find when the next slot will be free from the heap
//                 if (!slotReleaseTimes.empty()) {
//                     SubsecondTime nextAvailableTime = slotReleaseTimes.top();
//                     if (nextAvailableTime > currentTime) {
//                         stall_time = (nextAvailableTime - currentTime);
//                         std::cout << "RX FIFO full. Next slot free in " << stall_time << " cycles." << std::endl;
//                     }

//                 }
//             }

//             // Simulate processing delay
//             SubsecondTime processingDelay = SubsecondTime::Zero();

//             RxDescriptor rx_desc;

//             rx_desc.virtual_page = Sim()->getVirtuOS()->allocate_rx_descriptor(

//             pcie_protocol->processDataFromNIC(pkt.data, rx_desc.virtual_page, currentTime+stall_time+processingDelay);

//             slotReleaseTimes.push(currentTime + stall_time + processingDelay);

//             rxFifo.push(make_pair(pkt, currentTime + stall_time + processingDelay));

//             std::cout << "[NIC] Packet enqueued: packet will be dequeued at " << currentTime + stall_time + processingDelay << std::endl;

//         } else {
//             std::cerr << "RX is disabled. Packet dropped." << std::endl;
//         }
//     }

//     void BasicNIC::cleanupRxFifo(SubsecondTime currentTime) {
//         while (!rxFifo.empty() && rxFifo.front().second <= currentTime) {
//             rxFifo.pop();
//         }
//     }

//     void BasicNIC::transmit() {

//     }

//     void BasicNIC::processReceivedPacket(Packet& pkt) {

//     }

//     void BasicNIC::scheduleEvent(uint64_t delay, std::function<void()> action) {

//     }

//     void BasicNIC::cleanupReleasedSlots(SubsecondTime currentTime) {

//     }

//     void BasicNIC::advanceTime() {

//     }

//     void BasicNIC::processEvents() {
        
        
//     }

//     void BasicNIC::simulate() {

        
//     }



// }