#include "trace_manager.h"
#include "trace_thread.h"
#include "simulator.h"
#include "thread_manager.h"
#include "hooks_manager.h"
#include "config.hpp"
#include "sim_api.h"
#include "stats.h"
#include "mimicos.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>

// #define DEBUG

TraceManager::TraceManager()
   : m_monitor(new Monitor(this)), m_threads(0), m_num_threads_started(0), m_num_threads_running(0), m_done(0), m_stop_with_first_app(Sim()->getCfg()->getBool("traceinput/stop_with_first_app")), m_app_restart(Sim()->getCfg()->getBool("traceinput/restart_apps")), m_emulate_syscalls(Sim()->getCfg()->getBool("traceinput/emulate_syscalls")), m_num_apps(Sim()->getCfg()->getInt("traceinput/num_apps")), m_num_apps_nonfinish(m_num_apps), m_app_info(m_num_apps), m_tracefiles(m_num_apps), m_responsefiles(m_num_apps)
{
   setupTraceFiles(0);
}

void TraceManager::setupTraceFiles(int index)
{
#ifdef DEBUG
   std::cout << "TraceManager::setupTraceFiles Function" << std::endl;
#endif
   m_trace_prefix = Sim()->getCfg()->getStringArray("traceinput/trace_prefix", index);
#ifdef DEBUG
   std::cout << "trace_prefix: " << m_trace_prefix << std::endl;
#endif

   if (m_emulate_syscalls)
   {
     if (m_trace_prefix == "")
     {
       std::cerr << "Error: a trace prefix is required when emulating syscalls." << std::endl;
       exit(1);
     }
   }

   if (m_trace_prefix != "")
   {
     for (UInt32 i = 0; i < m_num_apps; i++)
     {
       m_tracefiles[i] = getFifoName(i, 0, false /*response*/, false /*create*/);
#ifdef DEBUG
       std::cout << "m_tracefiles[" << i << "]: " << m_tracefiles[i] << std::endl;
#endif

       m_responsefiles[i] = getFifoName(i, 0, true /*response*/, false /*create*/);
     }
   }
   else
   {
     for (UInt32 i = 0; i < m_num_apps; i++)
     {
       m_tracefiles[i] = Sim()->getCfg()->getStringArray("traceinput/thread_" + itostr(i), index);
#ifdef DEBUG
       std::cout << "m_tracefiles[" << i << "]: " << m_tracefiles[i] << std::endl;
#endif
     }
   }
}

void TraceManager::init()
{
#ifdef DEBUG
   std::cout << "TraceManager::init Function" << std::endl;
#endif
   // print threads
#ifdef DEBUG
   std::cout << "Number of apps: " << m_num_apps << std::endl;
#endif
   for (UInt32 app_id = 0; app_id < m_num_apps; app_id++)
   {
#ifdef DEBUG
   std::cout << "[TraceManager] init calls newThread for" << 
   " app_id = " << app_id << 
   " creator_thread_id = " << INVALID_THREAD_ID << std::endl;
#endif
     newThread(app_id /*app_id*/, true /*first*/, false /*init_fifo*/, false /*spawn*/, SubsecondTime::Zero(), INVALID_THREAD_ID);
   }
}

String TraceManager::getFifoName(app_id_t app_id, UInt64 thread_num, bool response, bool create)
{
   String filename = m_trace_prefix + (response ? "_response" : "") + ".app" + itostr(app_id) + ".th" + itostr(thread_num) + ".sift";
   if (create)
     mkfifo(filename.c_str(), 0600);
   return filename;
}

thread_id_t TraceManager::createThread(app_id_t app_id, SubsecondTime time, thread_id_t creator_thread_id)
{
   // External version: acquire lock first
   ScopedLock sl(m_lock);

#ifdef DEBUG
   std::cout << "[TraceManager] createThread calls newThread for" << 
   " app_id = " << app_id << 
   " creator_thread_id = " << creator_thread_id << std::endl;
#endif
   return newThread(app_id, false /*first*/, true /*init_fifo*/, true /*spawn*/, time, creator_thread_id);
}

thread_id_t TraceManager::newThread(app_id_t app_id, bool first, bool init_fifo, bool spawn, SubsecondTime time, thread_id_t creator_thread_id)
{
   // Internal version: assume we're already holding the lock

   assert(static_cast<decltype(app_id)>(m_num_apps) > app_id);

   // @hsongara: Instantiate applications
   // For non-virtualized environments:
   // Create (only) one App Instance for the Host, this is the application running natively on the system
   // For virtualized environments:
   // Create one App Instance for the application in the VM, and one for the hypervisor application

   if (Sim()->isVirtualizedSystem() == true) {
      app_id_t vm_id = 0;
      Sim()->getMimicOS()->createApplication(vm_id);
      Sim()->getMimicOS_VM()->createApplication(app_id);
   }
   else {
      std::cout << "Creating new application with app id = " << app_id << std::endl;
      Sim()->getMimicOS()->createApplication(app_id);
   }

   String tracefile = "", responsefile = "";
   int thread_num;
   if (first)
   {
     m_app_info[app_id].num_threads = 1;
     m_app_info[app_id].thread_count = 1;

#ifdef DEBUG
     // Print app_id, num_therads, thread_count using std::cout
     std::cout << "[TraceManager] Creating FIRST thread for application " << app_id << " with thread count: " << m_app_info[app_id].thread_count << std::endl;
#endif
     Sim()->getHooksManager()->callHooks(HookType::HOOK_APPLICATION_START, (UInt64)app_id);
     Sim()->getStatsManager()->logEvent(StatsManager::EVENT_APP_START, SubsecondTime::MaxTime(), INVALID_CORE_ID, INVALID_THREAD_ID, (UInt64)app_id, 0, "");
     thread_num = 0;

     if (!init_fifo)
     {
       tracefile = m_tracefiles[app_id];
       if (m_responsefiles.size())
         responsefile = m_responsefiles[app_id];
     }
   }
   else
   {
     m_app_info[app_id].num_threads++;
     thread_num = m_app_info[app_id].thread_count++;
#ifdef DEBUG
     // Print app_id, num_therads, thread_count using std::cout
     std::cout << "[TraceManager] Creating NEW thread for application " << app_id << " with thread count: " << m_app_info[app_id].thread_count << std::endl;
#endif
   }

   if (init_fifo)
   {
     tracefile = getFifoName(app_id, thread_num, false /*response*/, true /*create*/);
#ifdef DEBUG
     std::cout << "tracefile: " << tracefile << std::endl;
#endif
     if (m_responsefiles.size()){
       responsefile = getFifoName(app_id, thread_num, true /*response*/, true /*create*/);
#ifdef DEBUG
       std::cout << "responsefile: " << responsefile << std::endl;
#endif
     }

   }

   m_num_threads_running++;
   std::cout << "[TraceManager] newThread: tracefile='" << tracefile << "' responsefile='" << responsefile << "' thread_num=" << thread_num << std::endl;
   Thread *thread = Sim()->getThreadManager()->createThread(app_id, creator_thread_id);
   TraceThread *tthread = new TraceThread(thread, time, tracefile, responsefile, app_id, init_fifo /*cleaup*/);
   m_threads.push_back(tthread);

#ifdef DEBUG
   std::cout << "[TraceManager] Trace Thread (tthread)'s current SiftReader is IDentified by name: " << tthread->getSiftReader()->getFilename() << std::endl;
   std::cout << "[TraceManager] Number of Trace Threads in the system: " << m_threads.size()  << std::endl;
#endif

   /* @kanellok: If userspace MimicOS is enabled, we set the current SIFT reader to the Kernel SIFT reader
      for the first thread of the first application, which is the MimicOS thread.
   */

   bool userspace_mimicos_enabled = Sim()->getCfg()->getBool("general/enable_userspace_mimicos");
   if (app_id == 0 && thread_num == 0)
   {
      // This is the first thread of the first application, set it as the MimicOS if userspace MimicOS is enabled
      std::cout << "[TraceManager] Setting trace readers for the first application" << std::endl;
      if (userspace_mimicos_enabled) {
         // This is the first thread of the first application, set it as the MimicOS thread
         std::cout << "[TraceManager] Setting kernel trace reader for the first application" << std::endl;
         tthread->setKernelSIFTRreader(tthread->getSiftReader());

         std::cout << "[TraceManager] Setting current trace reader for the first application as SIFT App Reader" << std::endl;
         // We set the current sift reader to the default trace reader which represents the MimicOS trace
         tthread->setCurrentSiftReader(tthread->getSiftReader());


         setKernelTraceReader(tthread->getSiftReader());
         tthread->setIsKernelPthread(true);
      }
      else {
         // Default path: set current sift reader to the main trace reader
         // so that handleAccessMemory/getLength/getPosition work correctly
         tthread->setCurrentSiftReader(tthread->getSiftReader());
      }
   }
   else if (userspace_mimicos_enabled && app_id >= 1)
   {
      /* Stage A (Apr 15 2026, extended Stage 1 Apr 18 2026): any userspace app
         TraceThread — first thread of a fork-spawned app (thread_num==0) OR a
         worker pthread_create'd inside a live app (thread_num>=1).  Both need
         the same wiring:
            - kernel_trace = shared main-kernel SIFT (for fault routing)
            - app_trace    = this TraceThread's own SIFT (its PIN thread's pipe)
            - current      = app_trace (start in user mode)
         On fault, m_run_func_with_userpace_mimicos flips current to kernel;
         on SimContextSwitch, back to app. */
      Sift::Reader* kernel = getKernelTraceReader();
      if (kernel == nullptr) {
         std::cerr << "[TraceManager] ERROR: userspace_mimicos_enabled but no kernel SIFT "
                      "reader registered yet (app_id=" << app_id << ")" << std::endl;
      } else {
         tthread->setKernelSIFTRreader(kernel);
         tthread->setAppSiftReader(tthread->getSiftReader());
         tthread->setCurrentSiftReader(tthread->getSiftReader());  /* start on app */
         /* Stage 2B.1 (Apr 18 2026): register this user-thread Reader in the
            pool so SimContextSwitchTo can switch any other core's TraceThread
            to it.  Key is encode(app_id, thread_num). */
         m_app_reader_pool.registerReader(
            AppReaderPool::encodeId(app_id, (UInt32)thread_num),
            tthread->getSiftReader());
         std::cout << "[TraceManager] Stage 1: bound app_id=" << app_id
                   << " thread_num=" << thread_num
                   << " to shared kernel SIFT (kernel=" << kernel->getFilename()
                   << ", app=" << tthread->getSiftReader()->getFilename()
                   << ") pool_size=" << m_app_reader_pool.size() << std::endl;
      }
   }
   else if (userspace_mimicos_enabled && app_id == 0 && thread_num >= 1)
   {
      /* Kernel pthread: its own SIFT pipe is the kernel reader.
         An app reader (trace or live) may be injected later via
         createTraceBasedApplication or injectForkedApp.  Until then,
         the mimicos run func stays in kernel mode (reading from its
         own SIFT pipe) because getCurrentSiftReader() != getAppSiftReader()
         is always true when m_app_trace is NULL. */
      tthread->setKernelSIFTRreader(tthread->getSiftReader());
      tthread->setCurrentSiftReader(tthread->getSiftReader());
      tthread->setIsKernelPthread(true);
      std::cout << "[TraceManager] Kernel pthread thread_num=" << thread_num
                << ": kernel reader = own SIFT, awaiting app injection" << std::endl;
   }


   if (spawn)
   {
     /* First thread of each app spawns only when initialization is done,
       next threads are created once we're running so spawn them right away. */
     tthread->spawn();
   }

#ifdef DEBUG
   std::cout << "[TraceManager] Thread with ThreadID = " << thread->getId() << " succesfully spawned" << std::endl;
#endif

   return thread->getId();
}

app_id_t TraceManager::injectForkedApp(thread_id_t creator_thread_id)
{
   ScopedLock sl(m_lock);

   app_id_t app_id = m_num_apps;
   m_num_apps++;
   m_num_apps_nonfinish++;
   app_info_t app_info;
   m_app_info.push_back(app_info);
   m_app_info[app_id].num_threads = 1;
   m_app_info[app_id].thread_count = 1;

   /* Create FIFOs the PIN child will open after execve. */
   String tracefile    = getFifoName(app_id, 0, false /*response*/, true /*create*/);
   String responsefile = getFifoName(app_id, 0, true  /*response*/, true /*create*/);

   Sim()->getMimicOS()->createApplication(app_id);

   Sift::Reader* app_reader = new Sift::Reader(tracefile.c_str(),
                                               responsefile.c_str(),
                                               m_app_info[0].num_threads + app_id);

   TraceThread* kernel_tthread = getTraceThread(0, creator_thread_id);
   if (kernel_tthread == nullptr) {
      std::cerr << "[TraceManager] injectForkedApp: no creator TraceThread (tid="
                << creator_thread_id << ")" << std::endl;
      delete app_reader;
      return -1;
   }

   setTraceReaderHandlers(app_reader, kernel_tthread);
   kernel_tthread->setAppSiftReader(app_reader);
   /* Stage 1 (Apr 18 2026): record which app_id is "on" this TraceThread
      when the app reader is active.  If the live app later pthread_creates
      a new thread, handleNewThreadFunc reads this to create the new
      TraceThread under the live app's app_id instead of the kernel's (0). */
   kernel_tthread->setInjectedAppId(app_id);
   /* Stage 2B.1 (Apr 18 2026): register this app reader in the pool so
      SimContextSwitchTo(app_id << 16 | 0) can find it.  Stage 3 (Apr 18
      2026): also stamp the kernel TraceThread with the running app_tid
      so preemption events carry the right id even though we installed
      the reader via the legacy inject path (not SimContextSwitchTo). */
   auto atid = AppReaderPool::encodeId(app_id, 0);
   m_app_reader_pool.registerReader(atid, app_reader);
   kernel_tthread->setRunningAppTid(atid);
   /* Do NOT swap current reader here; the kernel reader is mid-Read() when
      this handler fires.  The existing trace-based path (createTraceBased
      Application) follows the same convention — the run_func / SimContext
      Switch machinery flips current from kernel → app at the right moment. */

   std::cout << "[TraceManager] injectForkedApp: allocated app_id=" << app_id
             << " tracefile=" << tracefile
             << " (injected into tid=" << creator_thread_id << ")" << std::endl;

   return app_id;
}

AppReaderPool::app_thread_id_t TraceManager::registerSpawnedAppThread(
    app_id_t app_id, TraceThread* parent_tt)
{
   ScopedLock sl(m_lock);

   if ((UInt32)app_id >= m_app_info.size()) {
      std::cerr << "[TraceManager] registerSpawnedAppThread: app_id=" << app_id
                << " out of range (have " << m_app_info.size() << " apps)" << std::endl;
      return 0;
   }

   /* Allocate a fresh thread_num within this app (mirrors the newThread
      thread_count logic — each app has its own monotonically increasing
      thread counter, and the num_threads count is incremented for the
      app-exit bookkeeping path). */
   UInt32 thread_num = m_app_info[app_id].thread_count++;
   m_app_info[app_id].num_threads++;

   /* Stage 2D (Apr 18 2026): create the FIFO pair the child PIN thread will
      write to.  PIN's sift_recorder opens a file with the exact same name
      (app<app_id>.th<thread_num>.sift / _response) when threadStart fires;
      the FIFO must exist on disk before PIN opens for write. */
   String tracefile    = getFifoName(app_id, thread_num, false /*response*/, true /*create*/);
   String responsefile = getFifoName(app_id, thread_num, true  /*response*/, true /*create*/);

   /* Open the Reader.  Use a sensible id based on app+thread for diagnostics. */
   Sift::Reader* reader = new Sift::Reader(tracefile.c_str(), responsefile.c_str(),
                                           (app_id * 1000) + thread_num);

   /* Bind handlers to parent_tt as a placeholder.  When the target core's
      kernel calls SimContextSwitchTo(this_thread_id), the handler re-binds
      them to the new TraceThread so magic/syscall records dispatch there. */
   setTraceReaderHandlers(reader, parent_tt);

   AppReaderPool::app_thread_id_t atid =
       AppReaderPool::encodeId(app_id, thread_num);
   m_app_reader_pool.registerReader(atid, reader);

   std::cout << "[TraceManager] registerSpawnedAppThread: app_id=" << app_id
             << " thread_num=" << thread_num
             << " app_thread_id=0x" << std::hex << atid << std::dec
             << " tracefile=" << tracefile << std::endl;

   return atid;
}

app_id_t TraceManager::createApplication(SubsecondTime time, thread_id_t creator_thread_id)
{
   ScopedLock sl(m_lock);

#ifdef DEBUG
   std::cout << "TraceManager creates new application: " << m_num_apps << std::endl;
#endif

   app_id_t app_id = m_num_apps;
   m_num_apps++;
   m_num_apps_nonfinish++;

   app_info_t app_info;
   m_app_info.push_back(app_info);


   newThread(app_id, true /*first*/, true /*init_fifo*/, true /*spawn*/, time, creator_thread_id);

   return app_id;
}

app_id_t TraceManager::createTraceBasedApplication(SubsecondTime time, char *trace, thread_id_t creator_thread_id)
{

   ScopedLock sl(m_lock);

   std::cout << "[TraceManager] createTraceBasedApplication: trace='" << trace
             << "' creator_tid=" << creator_thread_id << std::endl;

   /* Inject trace reader into the caller's TraceThread.  In single-core mode
      the caller is TraceThread 0 (the MimicOS main thread).  In multi-core
      mode the caller is the kernel pthread's TraceThread (e.g. thread_id=1).
      Either way, the TraceThread alternates between its kernel SIFT reader
      and this app reader on fault via m_run_func_with_userpace_mimicos. */

   if(creator_thread_id == 0){
      std::cout << "[TraceManager] MimicOS creates trace-based application with trace file: " << trace << std::endl;
   }

   Sift::Reader *app_trace_reader = new Sift::Reader(trace, "",(m_app_info[0].num_threads+1));

   // Find the TraceThread for the caller (app_id=0, thread_id=creator_thread_id)
   TraceThread* mimicos_trace_thread = getTraceThread(0, creator_thread_id);

   if (mimicos_trace_thread == nullptr) {
      std::cerr << "[TraceManager] createTraceBasedApplication: no TraceThread for (0, "
                << creator_thread_id << ")" << std::endl;
      delete app_trace_reader;
      return -1;
   }

   setTraceReaderHandlers(app_trace_reader, mimicos_trace_thread);

   mimicos_trace_thread->setAppSiftReader(app_trace_reader);
   /* Mark this as a replayable trace so the run-func can reopen the file
      on EOF rather than exiting the TraceThread. */
   mimicos_trace_thread->setAppTracePath(String(trace));

   std::cout << "[TraceManager] createTraceBasedApplication: injected into TraceThread(0,"
             << creator_thread_id << ")" << std::endl;

   return 0;
}
void TraceManager::signalStarted()
{
   ++m_num_threads_started;
}

void TraceManager::signalDone(TraceThread *thread, SubsecondTime time, bool aborted)
{
#ifdef DEBUG
   std::cout << "TraceManager::signalDone Function" << std::endl;
#endif
   ScopedLock sl(m_lock);

   // Make sure threads don't call signalDone twice (once through endApplication,
   //   and once the regular way), as this would throw off our counts
   if (thread->m_stopped)
   {
     return;
   }
   thread->m_stopped = true;

   app_id_t app_id = thread->getThread()->getAppId();
   m_app_info[app_id].num_threads--;

   if (!aborted)
   {
     if (m_app_info[app_id].num_threads == 0)
     {
       m_app_info[app_id].num_runs++;
       Sim()->getHooksManager()->callHooks(HookType::HOOK_APPLICATION_EXIT, (UInt64)app_id);
       Sim()->getStatsManager()->logEvent(StatsManager::EVENT_APP_EXIT, SubsecondTime::MaxTime(), INVALID_CORE_ID, INVALID_THREAD_ID, (UInt64)app_id, 0, "");

       if (m_app_info[app_id].num_runs == 1)
         m_num_apps_nonfinish--;

       if (m_stop_with_first_app)
       {
         // First app has ended: stop
         stop();
       }
       else if (m_num_apps_nonfinish == 0)
       {
         // All apps have completed at least once: stop
         stop();
       }
       else
       {
         // Stop condition not met. Restart app?
         if (m_app_restart)
         {
            newThread(app_id, true /*first*/, false /*init_fifo*/, true /*spawn*/, time, INVALID_THREAD_ID);
         }
       }
     }
   }

   m_num_threads_running--;

   /* Stop the whole simulation once every TraceThread has exited.
      In the multi-core userspace-MimicOS setup, all threads share
      app_id 0 (MimicOS kernel) so m_num_apps_nonfinish never reaches
      0 via the legacy app-id accounting — instead, we key off the
      live thread count.  This lets a live app finishing remove only
      its own TraceThread (while a replaying trace on another core
      keeps running) and only end simulation once no TraceThread
      remains. */
   if (m_num_threads_running == 0)
   {
      std::cout << "[TraceManager] All TraceThreads have exited — stopping simulation" << std::endl;
      stop();
   }
}

void TraceManager::endApplication(TraceThread *thread, SubsecondTime time)
{
   for (std::vector<TraceThread *>::iterator it = m_threads.begin(); it != m_threads.end(); ++it)
   {
     // Abort all threads in this application, except ourselves (we should end normally soon)
     if ((*it)->getThread()->getAppId() == thread->getThread()->getAppId() && *it != thread)
     {
       /* Stage 4 (Apr 18 2026): skip MimicOS kernel pthreads.  They all
          register under app_id=0 so the "same app_id = same app" heuristic
          here would falsely abort unrelated cores when any live app's
          SYS_exit_group fires — even though each kernel pthread is an
          independent per-core worker and should only exit when its own
          shutdown event arrives. */
       if ((*it)->isKernelPthread()) {
          continue;
       }
       // Ask thread to stop
       (*it)->stop();
       // Threads are often blocked on a futex in this case, so call signalDone in their place
       signalDone(*it, time, true /* aborted */);
     }
   }
}

void TraceManager::cleanup()
{
   for (std::vector<TraceThread *>::iterator it = m_threads.begin(); it != m_threads.end(); ++it)
     delete *it;
   m_threads.clear();

   m_num_threads_running = 0;
   m_app_info.clear();
   m_app_info.resize(m_num_apps);
   m_num_apps_nonfinish = m_num_apps;
}

void TraceManager::cleanupAllThreads()
{
   std::cout << "[TraceManager] Cleaning up all trace threads" << std::endl;
   // Clean up ChampSim instruction caches on all trace threads to avoid memory leaks
   for (std::vector<TraceThread *>::iterator it = m_threads.begin(); it != m_threads.end(); ++it)
     (*it)->cleanupChampSimCache();
}

TraceManager::~TraceManager()
{
   cleanup();
}

void TraceManager::start()
{
   m_monitor->spawn();
   for (std::vector<TraceThread *>::iterator it = m_threads.begin(); it != m_threads.end(); ++it)
     (*it)->spawn();
}

void TraceManager::stop()
{
   /* Stage 4 (Apr 18 2026): stop() can now be called from two paths —
      the ROI-leave path (hooks on last-app-leaves-ROI) and our new
      wasEverLiveAndNowZero trigger in notifyAppExitAndSwitchToKernel.
      Guard against double-entry: stopAll() writing to already-closed
      response pipes NULL-derefs the Reader's response stream. */
   bool expected = false;
   if (!m_shutdown_requested.compare_exchange_strong(expected, true,
                                                     std::memory_order_acq_rel)) {
      return;
   }

   // End of region-of-interest when running Sniper inside Sniper
   SimRoiEnd();

   // Signal threads to stop.
   for (std::vector<TraceThread *>::iterator it = m_threads.begin(); it != m_threads.end(); ++it)
     (*it)->stop();
   /* Stage 2 (Apr 18 2026): also tell every pool Reader's PIN-side Writer
      to shut down.  Without this, an idle/descheduled live-app thread's
      PIN process stays blocked on a magic-instruction response and hits
      an fread short-read assertion in Sift::Writer::Magic during teardown.
      Stopping their front-ends lets them exit cleanly.  initResponse can
      fail if the PIN thread has already exited (the response FIFO has no
      reader) — that is logged by Sift::Reader and handled gracefully. */
   m_app_reader_pool.stopAll();
   // Give threads some time to end.
   sleep(1);
   
   // Clean up ChampSim instruction caches on all trace threads to avoid memory leaks
   // (TraceThread destructors are never called because TraceManager is never deleted)
   for (std::vector<TraceThread *>::iterator it = m_threads.begin(); it != m_threads.end(); ++it)
     (*it)->cleanupChampSimCache();
   
   // Some threads may be blocked (SIFT reader, syscall, etc.). Don't wait for them or we'll deadlock.
   m_done.signal();
   // Notify SIFT recorders that simulation is done,
   // and that they should hide their errors when writing to an already-closed SIFT pipe.
   mark_done();
}

void TraceManager::mark_done()
{
   FILE *fp = fopen((m_trace_prefix + ".sift_done").c_str(), "w");
   fclose(fp);
}

void TraceManager::wait()
{
   m_done.wait();
}

void TraceManager::run()
{
#ifdef DEBUG
   std::cout << "TraceManager::run Function" << std::endl;
#endif
   start();
#ifdef DEBUG
   std::cout << "TraceManager::wait Function" << std::endl;
#endif
   wait();
#ifdef DEBUG
   std::cout << "TraceManager::wait after Function" << std::endl;
#endif
}

UInt64 TraceManager::getProgressExpect()
{
   return 1000000;
}

UInt64 TraceManager::getProgressValue()
{
   UInt64 value = 0;
   for (std::vector<TraceThread *>::iterator it = m_threads.begin(); it != m_threads.end(); ++it)
   {
     uint64_t expect = (*it)->getProgressExpect();
     if (expect)
     {
       UInt64 this_value = 1000000 * (*it)->getProgressValue() / expect;

       if (m_stop_with_first_app)
         // Stop with first app
         value = std::max(value, this_value);
       else
         // Stop with last app
         value = std::min(value, this_value);
     }
   }
   return value;
}

// This should only be called when already holding the thread lock to prevent migrations while we scan for a core id match
void TraceManager::accessMemory(int core_id, Core::lock_signal_t lock_signal, Core::mem_op_t mem_op_type, IntPtr d_addr, char *data_buffer, UInt32 data_size)
{
   for (std::vector<TraceThread *>::iterator it = m_threads.begin(); it != m_threads.end(); ++it)
   {
     TraceThread *tthread = *it;
     assert(tthread != NULL);
     if (tthread->getThread() && tthread->getThread()->getCore() && core_id == tthread->getThread()->getCore()->getId())
     {
       if (tthread->m_stopped)
       {
         // FIXME: should we try doing the memory access through another thread in the same application?
         LOG_PRINT_WARNING_ONCE("accessMemory() called but thread already killed since application ended");
         return;
       }
       tthread->handleAccessMemory(lock_signal, mem_op_type, d_addr, data_buffer, data_size);
       return;
     }
   }
   LOG_PRINT_ERROR("Unable to find core %d", core_id);
}

TraceManager::Monitor::Monitor(TraceManager *manager)
   : m_manager(manager)
{
}

TraceManager::Monitor::~Monitor()
{
   delete m_thread;
}

void TraceManager::Monitor::run()
{
   // Set thread name for Sniper-in-Sniper simulations
   String threadName("trace-monitor");
   SimSetThreadName(threadName.c_str());

   UInt64 n = 0;
   while (true)
   {
     if (m_manager->m_num_threads_started > 0)
       break;

     if (n == 10)
     {
       fprintf(stderr, "[SNIPER] WARNING: No SIFT connections made yet. Waiting...\n");
     }
     else if (n == 60)
     {
       fprintf(stderr, "[SNIPER] ERROR: Could not establish SIFT connection, aborting! Check benchmark-app*.log for errors.\n");
       exit(1);
     }

     sleep(1);
     ++n;
   }
}

void TraceManager::Monitor::spawn()
{
   m_thread = _Thread::create(this);
   m_thread->run();
}

void TraceManager::endFrontEnd()
{
   for(std::vector<TraceThread *>::iterator it = m_threads.begin(); it != m_threads.end(); ++it){
      if(!(*it)->m_stopped)
         (*it)->frontEndStop();
   }
}

TraceThread* TraceManager::getTraceThread(app_id_t app_id, thread_id_t th)
{
   for (int i = 0; i < (int)m_threads.size(); ++i)
   {
      TraceThread* thread = m_threads[i];
      if (thread->getThread()->getAppId() == app_id && thread->getThread()->getId() == th)
         return thread;

   }
   return nullptr;
}

void TraceManager::setTraceReaderHandlers(Sift::Reader* reader, TraceThread* mimicos_trace_thread) {
   reader->setHandleInstructionCountFunc(TraceThread::__handleInstructionCountFunc, mimicos_trace_thread);
   reader->setHandleCacheOnlyFunc(TraceThread::__handleCacheOnlyFunc, mimicos_trace_thread);
   if (Sim()->getCfg()->getBool("traceinput/mirror_output"))
      reader->setHandleOutputFunc(TraceThread::__handleOutputFunc, mimicos_trace_thread);
   reader->setHandleSyscallFunc(TraceThread::__handleSyscallFunc, mimicos_trace_thread);
   reader->setHandleNewThreadFunc(TraceThread::__handleNewThreadFunc, mimicos_trace_thread);
   reader->setHandleJoinFunc(TraceThread::__handleJoinFunc, mimicos_trace_thread);
   reader->setHandleMagicFunc(TraceThread::__handleMagicFunc, mimicos_trace_thread);
   reader->setHandleEmuFunc(TraceThread::__handleEmuFunc, mimicos_trace_thread);
   reader->setHandleForkFunc(TraceThread::__handleForkFunc, mimicos_trace_thread);
   if (Sim()->getRoutineTracer())
      reader->setHandleRoutineFunc(TraceThread::__handleRoutineChangeFunc, TraceThread::__handleRoutineAnnounceFunc, mimicos_trace_thread);
}
