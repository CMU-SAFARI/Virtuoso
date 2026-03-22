#pragma once
#include <fstream>
#include "simulator.h"
#include "mimicos.h"
#include "debug_config.h"
#include "../../../include/memory_management/misc/vma.h"

namespace Sniper {
    namespace Spot {
        struct SpotSniperPolicy {
            //
            // === 1. Stats Registration (Sniper-specific) ===
            //
            template <typename Alloc>
            static void register_stats(const String &name, Alloc &alloc) {
                // Does not have any stats to register currently
            }

            //
            // === 2. Initialize (Sniper-specific logging setup) ===
            //
            template <typename Alloc>
            static void on_init(const String &name,
                                UInt64 memory_size,
                                UInt64 kernel_size,
                                int max_order,
                                const String &frag_type,
                                Alloc *alloc) 
            {
                std::string fname = std::string(Sim()->getConfig()->getOutputDirectory().c_str()) + "/" + "spot_allocator.log";
                alloc->log_stream.open(fname);   // attach to allocator
                if (!alloc->log_stream.is_open()) {
                    throw std::runtime_error("[SPOT_POLICY] Failed to open log file");
                }

                std::cout << "[MimicOS] SpOT Allocator" << std::endl;
                alloc->log_stream << "[MimicOS] Creating SpOT Allocator" << std::endl;
            }

            template <typename Alloc>
            static std::tuple<int, int64_t, IntPtr> find_VMA_specs(IntPtr address, 
                                                                   int core_id,
                                                                   Alloc* alloc)
            {
                auto& vma_list = Sim()->getMimicOS()->getVMA(core_id);
                int64_t current_offset = static_cast<int64_t>(-1);
                VMA* current_vma = nullptr;

                int vma_index = 0;

                // Iterate through the VMA list to find the VMA containing the address
                for (auto& vma : vma_list)
                {
                    if (vma.getBase() <= address && vma.getEnd() > address)
                    {
                        if (!vma.isAllocated())
                        {
                            // Allocate VMA
                            Sim()->getMimicOS()->setAllocatedVMA(core_id, vma_index);
                            current_vma = &vma;
                        }
                        else
                        {
                            // VMA has been allocated, use existing offset
                            current_offset = vma.getPhysicalOffset();
                            current_vma = &vma;
                        }
                        break;
                    }
                    vma_index++;
                }

                // If no VMA found, return sentinel values
                if (current_vma == nullptr)
                {
                    return std::make_tuple(-1, static_cast<int64_t>(-1), static_cast<IntPtr>(0));
                }

                IntPtr vma_size = current_vma->getEnd() - current_vma->getBase();

                return std::make_tuple(vma_index, current_offset, vma_size);
            }

            // Set the physical offset for the VMA
            template <typename Alloc>
            static void setPhysicalOffset(int core_id, int vma_index, int64_t offset, Alloc* alloc)
            {
                Sim()->getMimicOS()->setPhysicalOffset(core_id, vma_index, offset);
            }

            template <typename Alloc>
            static IntPtr getPhysicalOffset(int core_id, IntPtr address, Alloc* alloc)
            {
                IntPtr offset = Sim()->getMimicOS()->getPhysicalOffsetSpot(core_id, address);
                return offset;
            }

            template <typename Alloc>
            static void log_VMA_specs(int core_id, int vma_index, Alloc* alloc)
            {
                auto& vma_list = Sim()->getMimicOS()->getVMA(core_id);
                VMA& vma = vma_list[vma_index];

#if DEBUG_SPOT_ALLOCATOR >= DEBUG_BASIC
                alloc->log_stream << "[SpotAllocator] Current VMA Base: " << vma.getBase() << std::endl;
                alloc->log_stream << "[SpotAllocator] Current VMA End: " << vma.getEnd() << std::endl;
                alloc->log_stream << "[SpotAllocator] Current VMA Physical Offset: " << vma.getPhysicalOffset() << std::endl;
#endif
            }
        };
    }    
}