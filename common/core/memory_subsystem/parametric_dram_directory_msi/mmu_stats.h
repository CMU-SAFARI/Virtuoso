#ifndef MMU_STATS_H
#define MMU_STATS_H

#include "fixed_types.h"
#include "subsecond_time.h"

         struct {
            
           UInt64 tlb_hit;
           UInt64 utr_hit;
           UInt64 tlb_hit_l1;
           UInt64 tlb_hit_l2;
           UInt64 tlb_and_utr_miss;
         
           SubsecondTime tlb_hit_latency;
           SubsecondTime tlb_hit_l1_latency;
           SubsecondTime tlb_hit_l2_latency;

           SubsecondTime utr_hit_latency;
           SubsecondTime tlb_and_utr_miss_latency;
           SubsecondTime total_latency;

           UInt64 llc_miss_l1tlb_hit;
           UInt64 llc_miss_l2tlb_hit;
           UInt64 llc_miss_l2tlb_miss;

           UInt64 llc_hit_l1tlb_hit;
           UInt64 llc_hit_l2tlb_hit;
           UInt64 llc_hit_l2tlb_miss;


           SubsecondTime victima_latency;
           SubsecondTime l1c_hit_tlb_latency;
           SubsecondTime l2c_hit_tlb_latency;
           SubsecondTime nucac_hit_tlb_latency;
           
           UInt64 l1c_hit_tlb;
           UInt64 l2c_hit_tlb;
           UInt64 nucac_hit_tlb;

           SubsecondTime ptw_contention;


         } translation_stats;


#endif