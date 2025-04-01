// #include <iostream>
// #include <queue>
// #include "iommu.h"
// #include "mmu_base.h"
// #include "mmu_factory.h"
        
// using namespace ParametricDramDirectoryMSI;

// IOMMU::IOMMU(bool _ddio_enabled) : 
//         ddio_enabled(_ddio_enabled) 
// {
//         mmu = MMUFactory::createMemoryManagementUnit("iommu", NULL, NULL, NULL, "iommu");
// }

// void IOMMU::translateRxDescriptor(uint64_t virtual_page, SubsecondTime currentTime) {

//     IntPtr eip = 0;
//     IntPtr address = virtual_page << 12;

//     Core::lock_signal_t lock = Core::NONE;

//     mmu->performAddressTranslation(eip, address , false, lock, true, true);

// }
