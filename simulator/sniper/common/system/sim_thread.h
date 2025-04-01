#ifndef SIM_THREAD_H
#define SIM_THREAD_H

#include "_thread.h"
#include "fixed_types.h"
#include "network.h"

class SimThread : public Runnable
{
public:
   SimThread();
   ~SimThread();

   void spawn();

private:
   void run();

   static void terminateFunc(void *vp, NetPacket pkt);

   _Thread *m_thread;
};

#endif // SIM_THREAD_H
