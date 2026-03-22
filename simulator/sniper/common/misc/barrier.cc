#include "barrier.h"
#include "log.h"

Barrier::Barrier(int count)
   : m_count(count)
   , m_arrived(0)
   , m_leaving(0)
   , m_lock()
   , m_cond()
{
}

Barrier::~Barrier()
{
}

void Barrier::wait()
{
   while(m_leaving.load(std::memory_order_relaxed) > 0)
      sched_yield(); // Not everyone has left, wait a bit

   m_lock.acquire();
   ++m_arrived;

   if (m_arrived == m_count)
   {
      m_arrived = 0;
      m_leaving.store(m_count - 1, std::memory_order_relaxed);
      m_lock.release();
      m_cond.broadcast();
   }
   else
   {
      m_cond.wait(m_lock);
      m_leaving.fetch_sub(1, std::memory_order_relaxed);
      m_lock.release();
   }
}
