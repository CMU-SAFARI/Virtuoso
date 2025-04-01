// #include <iostream>
// #include <queue>
// #include <vector>
// #include "io_mmu.h"
// #include "subsecond_time.h"


// class PCIeDevice {
// public:
//     struct Buffer {
//         std::queue<std::pair<std::string,SubsecondTime>> dataQueue; // Queue of data packets represented as vectors of chars
//     };

//     struct Credits {
//         int availableCredits; // Number of available credits for PCIe transactions
//     };

    
//     Buffer buffer;
//     Credits pcieCredits;
//     IOMMU *iommu;

//     SubsecondTime processingDelay;

//     class Compare
//     {
//         public:
//             bool operator()(SubsecondTime a, SubsecondTime b)
//             {
//                 return a > b;
//             }
//     };

//     std::priority_queue<SubsecondTime, std::vector<SubsecondTime>, Compare> creditReleaseTimes;


    

//     PCIeDevice();

//     // Simulate receiving data into the PCIe device
//     SubsecondTime processDataFromNIC(std::string data, IntPtr rx_desc_address, SubsecondTime currentTime);
//     void cleanupReleasedSlots(SubsecondTime currentTime);
//     void cleanupReleasedCredits(SubsecondTime currentTime);


// };