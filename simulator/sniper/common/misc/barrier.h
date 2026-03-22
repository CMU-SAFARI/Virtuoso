#ifndef __BARRIER_H
#define __BARRIER_H

#include "fixed_types.h"
#include "cond.h"
#include <atomic>

class Barrier
{
   public:
      Barrier(int count);
      ~Barrier();

      void wait();

   private:
      int m_count;
      int m_arrived;
      std::atomic<int> m_leaving;
      Lock m_lock;
      ConditionVariable m_cond;
};

#endif // __BARRIER_H
