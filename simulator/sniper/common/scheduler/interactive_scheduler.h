#ifndef __SCHEDULER_INTERACTIVE_H
#define __SCHEDULER_INTERACTIVE_H

#include "scheduler.h"
#include "simulator.h"
#include <vector>

class InteractiveScheduler : public Scheduler
{
   public:
   class ThreadInfo
      {
         public:
            ThreadInfo()
               : m_has_affinity(false)
               , m_explicit_affinity(false)
               , m_core_affinity(Sim()->getConfig()->getApplicationCores(), false)
               , m_core_running(INVALID_CORE_ID)
               , m_last_scheduled_in(SubsecondTime::Zero())
               , m_last_scheduled_out(SubsecondTime::Zero())
            {}
            /* affinity */
            void clearAffinity()
            {
               for(auto it = m_core_affinity.begin(); it != m_core_affinity.end(); ++it)
                  *it = false;
            }
            void setAffinitySingle(core_id_t core_id)
            {
               clearAffinity();
               addAffinity(core_id);
            }
            void addAffinity(core_id_t core_id) { m_core_affinity[core_id] = true; m_has_affinity = true; }
            bool hasAffinity(core_id_t core_id) const { return m_core_affinity[core_id]; }
            String getAffinityString() const;
            /* running on core */
            bool hasAffinity() const { return m_has_affinity; }
            bool hasExplicitAffinity() const { return m_explicit_affinity; }
            void setExplicitAffinity() { m_explicit_affinity = true; }
            void setCoreRunning(core_id_t core_id) { m_core_running = core_id; }
            core_id_t getCoreRunning() const { return m_core_running; }
            bool isRunning() const { return m_core_running != INVALID_CORE_ID; }
            /* last scheduled */
            void setLastScheduledIn(SubsecondTime time) { m_last_scheduled_in = time; }
            void setLastScheduledOut(SubsecondTime time) { m_last_scheduled_out = time; }
            SubsecondTime getLastScheduledIn() const { return m_last_scheduled_in; }
            SubsecondTime getLastScheduledOut() const { return m_last_scheduled_out; }
         private:
            bool m_has_affinity;
            bool m_explicit_affinity;
            std::vector<bool> m_core_affinity;
            core_id_t m_core_running;
            SubsecondTime m_last_scheduled_in;
            SubsecondTime m_last_scheduled_out;
      };

      InteractiveScheduler(ThreadManager *thread_manager);

      core_id_t threadCreate(thread_id_t);
      
   private:
         // Keyed by thread_id
      std::vector<ThreadInfo> m_thread_info;
      // Keyed by core_id
      std::vector<thread_id_t> m_core_thread_running;

      core_id_t findFirstFreeMaskedCore();
};

#endif // __SCHEDULER_STATIC_H
