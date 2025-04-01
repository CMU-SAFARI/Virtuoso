
// #include <iostream>
// #include <queue>
// #include <vector>
// #include "iio_pcie.h"


// PCIeDevice::PCIeDevice() {
//     processingDelay = SubsecondTime::NS(1); // Simulated processing delay for writing to the SRAM-based Buffer of the PCIe device
//     pcieCredits.availableCredits = 100; // Initialize with a set number of credits, e.g., 100 - 1 credit per PCIe transaction 
//     std::cout << "Initializing PCIe device with " << pcieCredits.availableCredits << " credits" << std::endl;

//     iommu = new IOMMU(true); // Initialize IOMMU with DDIO enabled

// }

// // Simulate receiving data into the PCIe device
// SubsecondTime PCIeDevice::processDataFromNIC(std::string data, IntPtr rx_desc_address, SubsecondTime currentTime) {
//     std::cout << "Starting processDataFromNIC" << std::endl;
//     std::cout << "Current time: " << currentTime.getNS() << " ns" << std::endl;

//     cleanupReleasedSlots(currentTime);
//     std::cout << "Released slots cleaned up" << std::endl;

//     cleanupReleasedCredits(currentTime);
//     std::cout << "Released credits cleaned up" << std::endl;

//     SubsecondTime credits_stall_time = SubsecondTime::Zero();

//     if (pcieCredits.availableCredits > 0) {
//         pcieCredits.availableCredits--;
//         std::cout << "Data received. Remaining credits: " << pcieCredits.availableCredits << std::endl;
//     } else {    
//         std::cout << "Not enough PCIe credits to receive data atm" << std::endl;
//         pcieCredits.availableCredits--;
//         credits_stall_time = creditReleaseTimes.top() - currentTime;
//         std::cout << "Credits stall time: " << credits_stall_time.getNS() << " ns" << std::endl;
//     }

//     SubsecondTime iommu_stall_time = SubsecondTime::Zero();
//     iommu->translateRxDescriptor(rx_desc_address, currentTime);
//     std::cout << "IOMMU translation done" << std::endl;

//     SubsecondTime total_stall_time = credits_stall_time + iommu_stall_time;
//     std::cout << "Total stall time: " << total_stall_time.getNS() << " ns" << std::endl;

//     buffer.dataQueue.push(std::make_pair(data, currentTime + total_stall_time + processingDelay));
//     std::cout << "Data pushed to buffer queue" << std::endl;

//     creditReleaseTimes.push(currentTime + total_stall_time);
//     std::cout << "Credit release time updated" << std::endl;

//     SubsecondTime resultTime = currentTime + total_stall_time + processingDelay;
//     std::cout << "Returning time: " << resultTime.getNS() << " ns" << std::endl;

//     return resultTime;
// }


// void PCIeDevice::cleanupReleasedSlots(SubsecondTime currentTime) {

//     std::cout << "Cleaning up released slots" << std::endl;

//     while (!buffer.dataQueue.empty())
//     {
//         std::cout << "Checking buffer queue" << std::endl;

//         if (buffer.dataQueue.front().second <= currentTime)
//         {
//             buffer.dataQueue.pop();
//         }
//         else
//         {
//             break;
//         }
//     }
        
// }

// void PCIeDevice::cleanupReleasedCredits(SubsecondTime currentTime) {

//     std::cout << "Cleaning up released credits" << std::endl;

//     if(creditReleaseTimes.empty())
//     {
//         std::cout << "No credits to release" << std::endl;
//     }
    
//     while (!creditReleaseTimes.empty())
//     {
//         if (creditReleaseTimes.top() <= currentTime)
//         {
//             creditReleaseTimes.pop();
//             pcieCredits.availableCredits++;
//         }
//         else
//         {
//             break;
//         }
//     }

// }
