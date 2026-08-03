#ifndef __TRACE_MANAGER_H
#define __TRACE_MANAGER_H

#include "fixed_types.h"
#include "semaphore.h"
#include "core.h" // for lock_signal_t and mem_op_t
#include "_thread.h"
#include "sift_reader.h"
#include <vector>
#include <unordered_map>
#include <mutex>
#include <atomic>

class TraceThread;

/* ---------------------------------------------------------------------
 * Stage 2 (Apr 18 2026): AppReaderPool
 *
 * A process-wide registry mapping an opaque "app_thread_id" to the
 * Sift::Reader for that user thread's PIN SIFT pipe.  MimicOS schedules
 * user threads onto kernel pthreads' TraceThreads by issuing
 *   SimContextSwitchTo(app_thread_id)
 * which looks up the reader in this pool and installs it as the calling
 * core's app_reader + current_reader.
 *
 * The id is encoded as ((app_id << 16) | thread_num) so MimicOS can
 * synthesize the id of the main thread of a new live app before it has
 * started running (= app_id << 16).
 * ------------------------------------------------------------------- */
class AppReaderPool
{
   public:
      typedef uint64_t app_thread_id_t;

      /* Stage 3.1 (Apr 18 2026): per-app entry.  The pool tracks one of
         these per user thread.  `saved_inst` / `saved_next_inst` carry
         the (inst, next_inst) pair that the thread was about to execute
         when it was preempted; the next SimContextSwitchTo to this id
         restores the pair onto the target TraceThread's `to_be_replayed`
         slots so execution resumes exactly where it left off.  `has_saved`
         gates the restore — only pay the cost when a preemption actually
         occurred. */
      struct Entry {
         Sift::Reader*      reader = nullptr;
         Sift::Instruction  saved_inst;
         Sift::Instruction  saved_next_inst;
         bool               has_saved = false;
      };

      static app_thread_id_t encodeId(app_id_t app_id, UInt32 thread_num) {
         return ((app_thread_id_t)app_id << 16) | (thread_num & 0xffff);
      }

      void registerReader(app_thread_id_t id, Sift::Reader* reader) {
         std::lock_guard<std::mutex> lk(m_lock);
         bool is_new = (m_entries.find(id) == m_entries.end());
         auto& e = m_entries[id];
         e.reader    = reader;
         e.has_saved = false;
         e.saved_inst.sinst = nullptr;
         e.saved_next_inst.sinst = nullptr;
         if (is_new) {
            m_live_count++;
            m_ever_live = true;
         }
      }

      Sift::Reader* lookup(app_thread_id_t id) {
         std::lock_guard<std::mutex> lk(m_lock);
         auto it = m_entries.find(id);
         return it == m_entries.end() ? nullptr : it->second.reader;
      }

      /* Return a copy of the entry (safe read under lock).  Callers that
         need to restore saved state after a SimContextSwitchTo use this. */
      Entry lookupEntry(app_thread_id_t id) {
         std::lock_guard<std::mutex> lk(m_lock);
         auto it = m_entries.find(id);
         return it == m_entries.end() ? Entry{} : it->second;
      }

      void stashSavedPair(app_thread_id_t id,
                          const Sift::Instruction& inst,
                          const Sift::Instruction& next_inst) {
         std::lock_guard<std::mutex> lk(m_lock);
         auto it = m_entries.find(id);
         if (it == m_entries.end()) return;
         it->second.saved_inst = inst;
         it->second.saved_next_inst = next_inst;
         it->second.has_saved = true;
      }

      void clearSavedPair(app_thread_id_t id) {
         std::lock_guard<std::mutex> lk(m_lock);
         auto it = m_entries.find(id);
         if (it == m_entries.end()) return;
         it->second.has_saved = false;
         it->second.saved_inst.sinst = nullptr;
         it->second.saved_next_inst.sinst = nullptr;
      }

      /* Stage 4.2b (Apr 18 2026): unregister removes the entry and
         calls frontEndStop on the Reader so the PIN-side Writer gets a
         clean RecOtherShutdown response.  We do NOT delete the Reader
         here: the lifetime model assumes Readers live until process
         exit (some paths register &m_trace which is a value member,
         and even heap-allocated Readers may still be referenced by
         TraceThread::m_app_trace/m_current_sift_reader on other cores).
         The m_live_count drops; if it reaches zero after having been
         positive, the caller is responsible for checking
         wasEverLiveAndNowZero() to end the simulation. */
      void unregisterReader(app_thread_id_t id) {
         Sift::Reader* reader = nullptr;
         {
            std::lock_guard<std::mutex> lk(m_lock);
            auto it = m_entries.find(id);
            if (it == m_entries.end()) return;
            reader = it->second.reader;
            m_entries.erase(it);
            if (m_live_count > 0) m_live_count--;
         }
         if (reader) {
            reader->frontEndStop();
         }
      }

      /* Stage 4.2a (Apr 18 2026): caller (usually the SimReceiveMessage
         handler right after posting a thread_exit) polls this to know
         when every user thread has ended so it can call TraceManager::stop().
         Returns true exactly once (by design) — further calls return false
         even if the count is still zero. */
      bool wasEverLiveAndNowZero() {
         std::lock_guard<std::mutex> lk(m_lock);
         if (m_ever_live && m_live_count == 0 && !m_terminated) {
            m_terminated = true;
            return true;
         }
         return false;
      }

      size_t size() {
         std::lock_guard<std::mutex> lk(m_lock);
         return m_entries.size();
      }

      /* Stage 2 (Apr 18 2026): tell every registered Reader's front end
         (the PIN-side Writer) to shut down.  Each frontEndStop emits a
         RecOtherShutdown response record so the PIN Writer, which may
         be blocked waiting for a magic-instruction / new-thread response,
         unblocks cleanly instead of asserting on a short read at teardown.
         Called from TraceManager::stop() before the grace sleep. */
      void stopAll() {
         std::lock_guard<std::mutex> lk(m_lock);
         for (auto& kv : m_entries) {
            if (kv.second.reader) kv.second.reader->frontEndStop();
         }
      }

   private:
      std::unordered_map<app_thread_id_t, Entry> m_entries;
      std::mutex m_lock;
      /* Stage 4.2a (Apr 18 2026): live-app counter for sim-end detection. */
      size_t m_live_count  = 0;
      bool   m_ever_live   = false;
      bool   m_terminated  = false;
};

class TraceManager
{
   private:
      class Monitor : public Runnable
      {
         private:
            void run();
            _Thread *m_thread;
            TraceManager *m_manager;
         public:
            Monitor(TraceManager *manager);
            ~Monitor();
            void spawn();
      };

      struct app_info_t
      {
         app_info_t()
            : thread_count(1)
            , num_threads(1)
            , num_runs(0)
         {}
         UInt32 thread_count;       //< Index counter for new thread's FIFO name
         UInt32 num_threads;        //< Number of active threads for this app (when zero, app is done)
         UInt32 num_runs;           //< Number of completed runs
      };

      Monitor *m_monitor;
      std::vector<TraceThread *> m_threads;
      UInt32 m_num_threads_started;
      UInt32 m_num_threads_running;
      Semaphore m_done;
      const bool m_stop_with_first_app;
      const bool m_app_restart;
      const bool m_emulate_syscalls;
      UInt32 m_num_apps;
      UInt32 m_num_apps_nonfinish;  //< Number of applications that have yet to complete their first run
      std::vector<app_info_t> m_app_info;
      std::vector<String> m_tracefiles;
      std::vector<String> m_responsefiles;
      String m_trace_prefix;
      Lock m_lock;

      Sift::Reader *m_kernel_trace_reader;

      /* Stage 2B.1 (Apr 18 2026): app-thread Reader registry.
         Populated by injectForkedApp + newThread(app_id>=1); consulted
         by the SimContextSwitchTo magic handler. */
      AppReaderPool m_app_reader_pool;

      /* Stage 4 (Apr 18 2026): set by stop() so an idle SimReceiveMessage
         handler waiting on an empty event queue can exit its spin. */
      std::atomic<bool> m_shutdown_requested{false};

      String getFifoName(app_id_t app_id, UInt64 thread_num, bool response, bool create);
      thread_id_t newThread(app_id_t app_id, bool first, bool init_fifo, bool spawn, SubsecondTime time, thread_id_t creator_thread_id);

      friend class Monitor;

   public:
      /* Public so TraceThread can wire new app readers on replay. */
      void setTraceReaderHandlers(Sift::Reader* reader, TraceThread* trace_thread);

      AppReaderPool& getAppReaderPool() { return m_app_reader_pool; }

      /* Stage 4 (Apr 18 2026): global shutdown flag the SimReceiveMessage
         handler polls so an idle kernel pthread's spin loop can exit
         cleanly when TraceManager::stop() has been called. */
      bool isDone() const { return m_shutdown_requested.load(std::memory_order_acquire); }

      /* Stage 2D (Apr 18 2026): register a new app thread spawned by a live
         app via pthread_create.  Does NOT create a Sniper Thread / TraceThread
         / core — the kernel schedules it onto an existing kernel pthread's
         TraceThread via SimContextSwitchTo.
           - Allocates a fresh thread_num within `app_id`.
           - Creates the FIFO pair + Sift::Reader + binds handlers to
             `parent_tt` (handlers will be re-bound when SimContextSwitchTo
             moves the reader to a different TraceThread).
           - Registers the Reader in AppReaderPool under encode(app_id,
             thread_num).
         Returns the encoded app_thread_id. */
      AppReaderPool::app_thread_id_t registerSpawnedAppThread(
          app_id_t app_id, TraceThread* parent_tt);

      TraceManager();
      ~TraceManager();
      void init();
      void start();
      void stop();
      void mark_done();
      void wait();
      void run();
      void cleanup();
      void cleanupAllThreads();  // Clean up ChampSim caches on all trace threads
      void setupTraceFiles(int index);
      thread_id_t createThread(app_id_t app_id, SubsecondTime time, thread_id_t creator_thread_id);
      app_id_t createApplication(SubsecondTime time, thread_id_t creator_thread_id);
      app_id_t createTraceBasedApplication(SubsecondTime time, char* trace, thread_id_t creator_thread_id);
      /* Stage A (Apr 15 2026): fork+execve-spawned live app under userspace MimicOS.
         Allocates a new app_id + FIFO pair (so PIN child opens the right pipes),
         but does NOT create a new TraceThread.  Injects the new SIFT stream as
         the app-reader slot of the caller's TraceThread — matching the per-core
         "two readers, context-switch on fault" model. */
      app_id_t injectForkedApp(thread_id_t creator_thread_id);
      void signalStarted();
      void signalDone(TraceThread *thread, SubsecondTime time, bool aborted);
      void endApplication(TraceThread *thread, SubsecondTime time);
      void accessMemory(int core_id, Core::lock_signal_t lock_signal, Core::mem_op_t mem_op_type, IntPtr d_addr, char* data_buffer, UInt32 data_size);
      void endFrontEnd(); //Ask all trace_threads to send signal to front-end to shutdown

      TraceThread *getTraceThread(app_id_t app_id, thread_id_t th);

      Sift::Reader *getKernelTraceReader() const { return m_kernel_trace_reader; }
      void setKernelTraceReader(Sift::Reader *reader) { m_kernel_trace_reader = reader; }
      
      UInt64 getProgressExpect();
      UInt64 getProgressValue();
};

#endif // __TRACE_MANAGER_H
