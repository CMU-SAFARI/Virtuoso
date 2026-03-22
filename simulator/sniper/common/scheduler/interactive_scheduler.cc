#include "interactive_scheduler.h"
#include "core_manager.h"
#include "simulator.h"
#include "config.hpp"
#include "thread.h"
#include "log.h"

InteractiveScheduler::InteractiveScheduler(ThreadManager *thread_manager)
   : Scheduler(thread_manager)
{


}

core_id_t InteractiveScheduler::findFirstFreeMaskedCore()
{
   for (core_id_t core_id = 0; core_id < (core_id_t)Sim()->getConfig()->getApplicationCores(); core_id++)
   {
      if (Sim()->getCoreManager()->getCoreFromID(core_id)->getState() == Core::IDLE)
      {
         return core_id;
      }
   }
   return INVALID_CORE_ID;
}

core_id_t InteractiveScheduler::threadCreate(thread_id_t thread_id)
{
   
   core_id_t core_id = findFirstFreeMaskedCore();
   std::cout << "[InteractiveScheduler] Attempting to spawn thread " << thread_id << " on core " << core_id << std::endl;
   if (core_id == INVALID_CORE_ID)
   {
        std::cout << "[InteractiveScheduler] No free cores available to spawn thread " << thread_id << std::endl;
        //m_thread_info[thread_id].setCoreRunning(INVALID_CORE_ID);
        return INVALID_CORE_ID;
   }
   else{
         // Schedule the thread to the core
        std::cout << "[InteractiveScheduler] Found free core " << core_id << " for thread " << thread_id << std::endl;
        app_id_t app_id = Sim()->getThreadManager()->getThreadFromID(thread_id)->getAppId();
        //m_thread_info[thread_id].setCoreRunning(core_id);
        std::cout << "[InteractiveScheduler] Spawning application thread " << thread_id << " from application " << app_id << " to core " << core_id << std::endl;   

        return core_id;
   }

}

// core_id_t InteractiveScheduler::ContextSwitch(thread_id_t thread_id, thread_id_t kernel_thread, core_id_t core_id)
// {
//     // Deschedule the thread from its current core
//     if (m_thread_info[thread_id].isRunning()){
//         m_thread_info[thread_id].setCoreRunning(INVALID_CORE_ID);
//         std::cout << "[InteractiveScheduler] Descheduling thread " << thread_id << " from core " << core_id << std::endl;
//     }
//     else{
//         std::cout << "[InteractiveScheduler] Thread " << thread_id << " is not running on any core." << std::endl;
//     }

//     // Schedule the kernel thread to the core
//     m_thread_info[kernel_thread].setCoreRunning(core_id);
//     std::cout << "[InteractiveScheduler] Scheduling kernel thread " << kernel_thread << " to core " << core_id << std::endl;

//    return core_id;
// }
