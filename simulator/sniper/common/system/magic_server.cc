#include <iostream>

#include "magic_server.h"
#include "sim_api.h"
#include "simulator.h"
#include "thread_manager.h"
#include "logmem.h"
#include "performance_model.h"
#include "fastforward_performance_model.h"
#include "instruction.h"
#include "core.h"
#include "shmem_perf_model.h"
#include "memory_manager_base.h"
#include "barrier_sync_server.h"
#include "core_manager.h"
#include "dvfs_manager.h"
#include "hooks_manager.h"
#include "trace_manager.h"
#include "stats.h"
#include "timer.h"
#include "thread.h"
#include "mimicos.h"
#include "trace_thread.h"
#include "trace_manager.h"

#include "misc/exception_handler_base.h"

#include "debug_config.h"

MagicServer::MagicServer()
    : m_performance_enabled(false)
{
}

MagicServer::~MagicServer()
{
}

UInt64 MagicServer::Magic(thread_id_t thread_id, core_id_t core_id, UInt64 cmd, UInt64 arg0, UInt64 arg1)
{
   // ScopedLock sl(Sim()->getThreadManager()->getLock());

   return Magic_unlocked(thread_id, core_id, cmd, arg0, arg1);
}

UInt64 MagicServer::Magic_unlocked(thread_id_t thread_id, core_id_t core_id, UInt64 cmd, UInt64 arg0, UInt64 arg1)
{

#if DEBUG_MAGIC_SERVER >= DEBUG_DETAILED
   std::cout << "[Virtuoso] We are in Magic_unlocked" << std::endl;
#endif

   switch (cmd)
   {
   case SIM_CMD_ROI_TOGGLE:
      if (Sim()->getConfig()->getSimulationROI() == Config::ROI_MAGIC)
      {
         return setPerformance(!m_performance_enabled);
      }
      else
      {
         return 0;
      }
   case SIM_CMD_ROI_START:
      m_legacy_roi_active = true;
      Sim()->getHooksManager()->callHooks(HookType::HOOK_APPLICATION_ROI_BEGIN, 0);
      if (Sim()->getConfig()->getSimulationROI() == Config::ROI_MAGIC)
      {
         return setPerformance(true);
      }
      else
      {
         return 0;
      }
   case SIM_CMD_ROI_END:
      Sim()->getHooksManager()->callHooks(HookType::HOOK_APPLICATION_ROI_END, 0);
      if (Sim()->getConfig()->getSimulationROI() == Config::ROI_MAGIC)
      {
         return setPerformance(false);
      }
      else
      {
         return 0;
      }
   case SIM_CMD_ROI_EXPECT:
      return roiExpect(arg0);
   case SIM_CMD_THREAD_ROI_START:
      /* arg0 = unique participant id (PID), since multiple live processes
         may share one Sniper TraceThread (inject-forked-app model). */
      return threadRoiJoin((thread_id_t)arg0);
   case SIM_CMD_THREAD_ROI_END:
      return threadRoiLeave((thread_id_t)arg0);
   case SIM_CMD_WAIT_FOR_ROI:
   {
      /* Spin until performance models are enabled (by SimRoiStart or
         SimThreadRoiStart barrier).  Used by trace-based cores in mixed
         mode to wait for live apps to be ready. */
      printf("[SNIPER] WaitForRoi: waiting for perf model to enable...\n");
      fflush(NULL);
      while (!m_roi_ready.load(std::memory_order_acquire)) {
         usleep(1000);  // 1ms poll — trace core is idle anyway
      }
      printf("[SNIPER] WaitForRoi: perf model is on, proceeding\n");
      fflush(NULL);
      return 0;
   }
   case SIM_CMD_MHZ_SET:
      return setFrequency(arg0, arg1);
   case SIM_CMD_NAMED_MARKER:
   {
      char str[256];
      Core *core = Sim()->getCoreManager()->getCoreFromID(core_id);
      core->accessMemory(Core::NONE, Core::READ, arg1, str, 256, Core::MEM_MODELED_NONE);
      str[255] = '\0';

      MagicMarkerType args = {thread_id : thread_id, core_id : core_id, arg0 : arg0, arg1 : 0, str : str};
      Sim()->getHooksManager()->callHooks(HookType::HOOK_MAGIC_MARKER, (UInt64)&args);
      return 0;
   }
   case SIM_CMD_SET_THREAD_NAME:
   {
      char str[256];
      Core *core = Sim()->getCoreManager()->getCoreFromID(core_id);
      core->accessMemory(Core::NONE, Core::READ, arg0, str, 256, Core::MEM_MODELED_NONE);
      str[255] = '\0';

      Sim()->getStatsManager()->logEvent(StatsManager::EVENT_THREAD_NAME, SubsecondTime::MaxTime(), core_id, thread_id, 0, 0, str);
      Sim()->getThreadManager()->getThreadFromID(thread_id)->setName(str);
      return 0;
   }
   case SIM_CMD_CONTEXT_SWITCH:
   {
#if DEBUG_MAGIC_SERVER >= DEBUG_DETAILED
      std::cout << "[Virtuoso: Magic Instruction] We received a context switch command" << std::endl;
#endif
      Core *core = Sim()->getCoreManager()->getCoreFromID(core_id);
      core->getPerformanceModel()->drain();

      // We are returning from Context Switch...
      /* Multi-core userspace MimicOS: route the context switch to the
         TraceThread of the caller (the kernel pthread that issued
         SimContextSwitch), not hardcoded to TraceThread(0,0).  This lets
         per-core kernel pthreads each switch their own app reader. */
      auto trace_thread = Sim()->getTraceManager()->getTraceThread(0, thread_id);
      if (trace_thread == nullptr) {
         /* Fallback for single-core / legacy path. */
         trace_thread = Sim()->getTraceManager()->getTraceThread(0, 0);
      }
      auto thread = trace_thread->getThread();
#if DEBUG_MAGIC_SERVER >= DEBUG_DETAILED
      std::cout << "[Virtuoso: Magic Instruction] Thread with ID =  " << thread->getId()
                << " is currently running on core: " << thread->getCore()->getId() << std::endl;
#endif
      /* Removed hardcoded BEEFY_CORE assertion: in multi-core userspace MimicOS,
         each core has its own kernel pthread, so the assertion no longer holds. */

      // Restore the current Sift Reader to be the App one...
      trace_thread->setCurrentSiftReader(trace_thread->getAppSiftReader());
#if DEBUG_MAGIC_SERVER >= DEBUG_DETAILED
      std::cout << "[Virtuoso: Magic Instruction] Current SiftReader set to APP (thread "
                << thread_id << ")" << std::endl;
#endif
      return 42;
   }
   case SIM_CMD_CONTEXT_SWITCH_TO:
   {
      /* Stage 2B.2 (Apr 18 2026): schedule user thread arg0 (an
         AppReaderPool::app_thread_id) onto this kernel pthread's core.
         Look up its Reader in the pool and install as app_reader +
         current_reader on the caller's TraceThread.  Returns 42 so the
         Sift::Reader path treats it like SimContextSwitch and doesn't
         send a response record (we already "responded" by swapping
         readers and the next Read will come from the new reader). */
      Core *core = Sim()->getCoreManager()->getCoreFromID(core_id);
      core->getPerformanceModel()->drain();

      AppReaderPool::app_thread_id_t app_tid = arg0;
      Sift::Reader* target = Sim()->getTraceManager()->getAppReaderPool().lookup(app_tid);
      if (!target) {
         std::cerr << "[MagicServer] SimContextSwitchTo(" << app_tid
                   << "): no reader in pool; ignoring (thread_id=" << thread_id
                   << ", core_id=" << core_id << ")" << std::endl;
         return 42;
      }

      auto trace_thread = Sim()->getTraceManager()->getTraceThread(0, thread_id);
      if (!trace_thread) {
         trace_thread = Sim()->getTraceManager()->getTraceThread(0, 0);
      }
      /* Stage 2D (Apr 18 2026): re-bind the reader's callbacks to this
         TraceThread.  The reader was originally bound to the parent's TT
         (whichever one handled the new_thread magic), but record handlers
         must dispatch to whichever TT is currently reading the stream —
         otherwise a magic instruction in the user-thread's stream would
         fire handleMagicFunc on the wrong TraceThread. */
      Sim()->getTraceManager()->setTraceReaderHandlers(target, trace_thread);
      trace_thread->setAppSiftReader(target);
      trace_thread->setCurrentSiftReader(target);

      /* Stage 3 (Apr 18 2026): mark that this TraceThread is now running
         the given user thread, and — if the user thread was previously
         preempted — restore its saved (inst, next_inst) pair via the
         to_be_replayed_* slots (same mechanism used after a page-fault
         context-switch). */
      trace_thread->setRunningAppTid(app_tid);
      auto entry = Sim()->getTraceManager()->getAppReaderPool().lookupEntry(app_tid);
      if (entry.has_saved) {
         trace_thread->setToBeReplayed(entry.saved_inst, entry.saved_next_inst);
         Sim()->getTraceManager()->getAppReaderPool().clearSavedPair(app_tid);
         std::cout << "[Virtuoso: Magic Instruction] SimContextSwitchTo(0x"
                   << std::hex << app_tid << std::dec
                   << "): restored saved (inst,next) for resumed thread" << std::endl;
      }

      std::cout << "[Virtuoso: Magic Instruction] SimContextSwitchTo(0x"
                << std::hex << app_tid << std::dec
                << "): installed reader on TraceThread tid=" << thread_id
                << " core=" << core_id << std::endl;
      return 42;
   }
   case SIM_CMD_GET_INJECTED_APP_ID:
   {
      /* Stage 3 (Apr 18 2026): return the live app_id injected into this
         kernel pthread's TraceThread, so MimicOS's kernel_worker can seed
         its runqueue after spawn_live_application. */
      auto tt = Sim()->getTraceManager()->getTraceThread(0, thread_id);
      if (!tt) return (UInt64)-1;
      return (UInt64)tt->getInjectedAppId();
   }
   case SIM_CMD_RECEIVE_MESSAGE:
   {
#if DEBUG_MAGIC_SERVER >= DEBUG_DETAILED
      std::cout << "[Virtuoso: Magic Instruction] We need to read the message from Sniper's MimicOS" << std::endl;
#endif
      Core *core = Sim()->getCoreManager()->getCoreFromID(core_id);
      /* Stage 4 (Apr 18 2026): pop from this core's event queue.
         Blocks until a queued event is available — the kernel pthread
         on an idle core calls SimReceiveMessage and waits here until
         some other core posts a new_thread event (or the fault path
         posts page_fault, or quantum fires, etc.).  If simulation
         shutdown fires while we're blocked, synthesize a "shutdown"
         event so the kernel's dispatch loop exits cleanly instead of
         asserting on an invalid message. */
      auto *mimic = Sim()->getMimicOS();
      MimicOS_NS::Message popped;
      /* Instrumentation (Apr 19 2026): count empty-queue polls and
         print periodically so we can see whether SimReceiveMessage's
         wait is a wall-time hot path without depending on the
         shutdown path running to completion. */
      static std::atomic<uint64_t> s_poll_empty_iters{0};
      static std::atomic<uint64_t> s_poll_calls{0};
      static std::atomic<uint64_t> s_poll_empty_immediate{0};  /* had event on first try */
      uint64_t my_call = s_poll_calls.fetch_add(1, std::memory_order_relaxed) + 1;
      bool had_to_wait = false;
      while (!mimic->popEvent((int)core_id, popped)) {
         if (Sim()->getTraceManager()->isDone()) {
            MimicOSProtocol::buildMessage(popped, "shutdown");
            break;
         }
         had_to_wait = true;
         s_poll_empty_iters.fetch_add(1, std::memory_order_relaxed);
         /* sched_yield (not usleep(1000)) — see notes in Stage 4 remediation. */
         sched_yield();
      }
      if (!had_to_wait) s_poll_empty_immediate.fetch_add(1, std::memory_order_relaxed);
      if ((my_call & (my_call - 1)) == 0 || (my_call % 10) == 0) {
         /* Print on every power-of-2 call AND every 10th call — gives us
            good resolution early and steady cadence later.  Flush so we
            see the line even on abrupt exit. */
         std::cerr << "[SimReceiveMessage] calls=" << my_call
                   << " immediate_hits=" << s_poll_empty_immediate.load()
                   << " empty_iters_total=" << s_poll_empty_iters.load()
                   << " avg_iters/call="
                   << ((double)s_poll_empty_iters.load() / my_call)
                   << std::endl;
         std::cerr.flush();
      }
      /* Use the popped event directly — its argv is owned by `popped`
         and lives to the end of this case block, which is past the
         core->accessMemory writes below. */
      MimicOS_NS::Message* message = &popped;
#if DEBUG_MAGIC_SERVER >= DEBUG_DETAILED
      std::cout << "[Virtuoso: Magic Instruction] Received message with " << message->argc << " arguments" << std::endl;
#endif
      //Write the message to the core's memory - First write argc
      core->accessMemory(Core::NONE, Core::WRITE, arg0, (char*)&message->argc, sizeof(int), Core::MEM_MODELED_NONE);

      // Then write argv
      for (int i = 0; i < message->argc; i++)
      {
         core->accessMemory(Core::NONE, Core::WRITE, arg1 + i * sizeof(uint64_t), (char*)&message->argv[i], sizeof(uint64_t), Core::MEM_MODELED_NONE);
#if DEBUG_MAGIC_SERVER >= DEBUG_DETAILED
         std::cout << "[Virtuoso: Magic Instruction] Argument " << i << ": " << message->argv[i] << std::endl;
#endif
      }

      /* Event already dequeued via popEvent above — nothing more to do. */
      return 0;
   }
   case SIM_CMD_MIMICOS_RESULT:
   {
#if DEBUG_MAGIC_SERVER >= DEBUG_DETAILED
      // This is a result from the MimicOS, we can process it here
      // @vlnitu: Interpret the message based on the protocol
      std::cout << "[Virtuoso: Magic Instruction] [magic_server.cc] Trace-based app received a MimicOS result command" << std::endl;
      std::cout << "[Virtuoso: Magic Instruction] We interpret the result based on the Page Fault - Response protocol" << std::endl;
#endif
      Core *core = Sim()->getCoreManager()->getCoreFromID(core_id);


      // We need to access the memory to get the result
      Sift::Message msg;
      msg.argc = arg0;

#if DEBUG_MAGIC_SERVER >= DEBUG_DETAILED
      std::cout << "[Virtuoso: Magic Instruction] Page Fault - Reponse protocol: argc = " << msg.argc << std::endl;
#endif

      msg.argv = new uint64_t[msg.argc];

#if DEBUG_MAGIC_SERVER >= DEBUG_DETAILED
      std::cout << "[Virtuoso: Magic Instruction] Page Fault - Reponse protocol: argv_ptr = " << std::hex << arg1 << std::dec << std::endl;
#endif



      for (int i = 0; i < (msg.argc); i++)
      {
         core->accessMemory(Core::NONE, Core::READ, arg1+ i * sizeof(uint64_t), (char*)&msg.argv[i], sizeof(uint64_t));
#if DEBUG_MAGIC_SERVER >= DEBUG_DETAILED
         std::cout << "[Virtuoso: Magic Instruction] Argument " << i << ": " << msg.argv[i] << std::endl;
#endif
      }

      // @vlnitu: virtuos.cc/poll_for_signal defines this protocol
      int exception_type_code = msg.argv[0];
      // The following arguments are deserialized in handle_exception
      uint64_t vpn = msg.argv[1];
      uint64_t ppn = msg.argv[2];
      uint64_t page_size = msg.argv[3];
      std::vector<UInt64> frames;
      int num_requested_frames = msg.argc - 4; // 4 for exception_type
      frames.reserve(num_requested_frames);
      for (int i = 0; i < num_requested_frames; i++ )
      {
         frames.push_back(msg.argv[4 + i]);
      }
      
#if DEBUG_MAGIC_SERVER >= DEBUG_BASIC
      std::cout << "[Virtuoso: Magic Instruction] Page Fault - Response protocol: argv[0] = exception_type_code = " << exception_type_code << std::endl;
      std::cout << "[Virtuoso: Magic Instruction] Page Fault - Response protocol: argv[1] = vpn = " << vpn << std::endl;
      std::cout << "[Virtuoso: Magic Instruction] Page Fault - Response protocol: argv[2] = ppn = " << ppn << std::endl;
      std::cout << "[Virtuoso: Magic Instruction] Page Fault - Response protocol: argv[3] = page_size = " << page_size << std::endl;

      std::cout << "[Virtuoso: Magic Instruction] Page Fault - Response protocol: argv[4] = frames.size() = " << frames.size() << std::endl;
      for (int i = 0; i < frames.size(); i++)
      {
         std::cout << "[Virtuoso: Magic Instruction] Page Fault - Response protocol: argv[4 + " << i << "] = frames[" << i << "] = " << frames[i] << std::endl;
      }  
#endif
      (void)vpn;
      (void)ppn;
      (void)page_size;

      // Invoke page fault handler
      // @vnitu: Invoke Exception Handler, w/ param0 = exception_type (i.e.,  PAGE_FAULT) + forward argv to exception_handler,
      // @vlnitu: on the exception handler side, argv will be interpreted differently, depending on the protocol (i.e., PAGE_FAULT)
      
      core->getExceptionHandler()->handle_exception(exception_type_code, msg.argc, msg.argv);

      /* Phase 5 fault-latency charge via core clock.  Set BOTH the
         core's perf-model m_elapsed_time (advances barrier.global_time
         at next sync) AND the shmem USER_THREAD (next memory access's
         start timestamp) here, at the last moment before the app
         resumes.  SpawnInstruction short-circuits to the protected
         PerformanceModel::setElapsedTime via the friend relationship. */
      {
         /* Phase 5 fault-latency charge.  Three-layer advance:
            (1) core's PerformanceModel::m_elapsed_time via SpawnInstruction
            (2) ShmemPerfModel USER_THREAD (next access start timestamp)
            (3) BarrierSyncServer::m_global_time via advanceGlobalTime —
                bypasses the per-quantum barrier-release cadence that
                was clamping our charge at ~530 cyc/fault.
            With (3) the full charged latency reaches sim.out's Cycles
            counter. */
         uint64_t cycles = Sim()->getMimicOS()->getFastFaultChargeCycles();
         if (cycles > 0) {
            const ComponentPeriod *period = core->getDvfsDomain();
            SubsecondTime cost = static_cast<SubsecondTime>(*period) * cycles;

            PerformanceModel *pm = core->getPerformanceModel();
            SubsecondTime now_core = pm->getElapsedTime();
            pm->queuePseudoInstruction(new SpawnInstruction(now_core + cost));

            ShmemPerfModel *spm = core->getShmemPerfModel();
            SubsecondTime now_shmem = spm->getElapsedTime(ShmemPerfModel::_USER_THREAD);
            spm->setElapsedTime(ShmemPerfModel::_USER_THREAD, now_shmem + cost);

            BarrierSyncServer *bss = dynamic_cast<BarrierSyncServer*>(
               Sim()->getClockSkewMinimizationServer());
            if (bss) {
               bss->advanceGlobalTime(now_core + cost);
            }
         }
      }
      return 0;
   }
   case SIM_CMD_FAST_FAULT:
   {
      /* Phase 5 batch-replay handler.
         MimicOS kernel ships the whole captured_accesses trace in ONE
         magic round-trip instead of executing N real loads/stores on
         its side.  This replaces the prior replay path where every
         access was a volatile load/store/atomic in the MimicOS
         pthread's instruction stream — those had to be simulated
         individually through the core's ROB, which dominated replay
         wall time (volatile fences, atomic cmpxchg stalls, SIMD memset
         unroll).  The magic path instead issues core->accessMemory
         calls directly from the simulator, bypassing the ROB per-access
         and avoiding SIFT serialisation overhead.

         Payload:
           argv[0] = SIM_CMD_FAST_FAULT marker (redundant with cmd)
           argv[1] = fp_id
           argv[2] = frame_pa       (reserved, unused)
           argv[3] = static_zero_cycles  (per-zero_sweep static latency
                                          in simulated cycles; 0 = use
                                          full 64×64B line-write expand)
           argv[4] = trace_len      (# CapturedAccess entries)
           argv[5] = trace_ptr      (-> std::vector<CapturedAccess>::data())
           argv[6] = cursor         (monotonic, for dep-chain variety)
         Each CapturedAccess is 16 B: {vaddr:8, op:1, size_log2:1,
         dep_flag:1, reserved:5}. */
      Core *core = Sim()->getCoreManager()->getCoreFromID(core_id);

      int argc = (int)arg0;
      if (argc < 7) {
         std::cerr << "[Virtuoso: SimFastFault] malformed payload, argc="
                   << argc << " (expected >=7)" << std::endl;
         return 0;
      }
      uint64_t argv[8] = {0};
      int to_read = argc < 8 ? argc : 8;
      for (int i = 0; i < to_read; i++) {
         core->accessMemory(Core::NONE, Core::READ,
                            arg1 + i * sizeof(uint64_t),
                            (char*)&argv[i], sizeof(uint64_t),
                            Core::MEM_MODELED_NONE);
      }
      uint64_t fp_id               = argv[1];
      uint64_t static_zero_cycles  = argv[3];
      uint64_t trace_len           = argv[4];
      uint64_t trace_ptr           = argv[5];
      uint64_t cursor              = argv[6];
      uint64_t flag_word           = (argc >= 8) ? argv[7] : 0;
      bool     dict_cache_enabled  = (flag_word & 0x1ULL) != 0;

      /* Read the CapturedAccess blob from guest memory. */
      constexpr size_t kEntryBytes = 16;
      constexpr size_t kMaxEntries = 256;
      if (trace_len > kMaxEntries) trace_len = kMaxEntries;

      struct CapturedAccessWire {
         uint64_t vaddr;
         uint8_t  op, size_log2, dep_flag;
         uint8_t  reserved[5];
      };
      static_assert(sizeof(CapturedAccessWire) == 16, "wire layout mismatch");

      /* Technique B (Phase 6): dictionary cache on the Sniper side.
         Keyed by (trace_ptr, trace_len).  The MimicOS vector data()
         pointer is stable after profile promotion (no more push_backs),
         so caching by (ptr, len) is safe.  Saves N×16 B of
         core->accessMemory READs per replay — the dominant handler
         cost on hot profiles. */
      struct DictKey {
         uint64_t ptr;
         uint64_t len;
         bool operator==(const DictKey& o) const { return ptr == o.ptr && len == o.len; }
      };
      struct DictKeyHash {
         size_t operator()(const DictKey& k) const noexcept {
            return std::hash<uint64_t>{}(k.ptr) ^ (std::hash<uint64_t>{}(k.len) << 1);
         }
      };
      static std::unordered_map<DictKey, std::vector<CapturedAccessWire>, DictKeyHash> s_trace_dict;
      static std::atomic<uint64_t> s_dict_hits{0}, s_dict_misses{0};

      CapturedAccessWire entries[kMaxEntries];
      const CapturedAccessWire *entry_ptr = entries;

      if (dict_cache_enabled && trace_len > 0) {
         DictKey key{trace_ptr, trace_len};
         auto it = s_trace_dict.find(key);
         if (it != s_trace_dict.end()) {
            entry_ptr = it->second.data();
            s_dict_hits.fetch_add(1, std::memory_order_relaxed);
         } else {
            for (size_t i = 0; i < trace_len; i++) {
               core->accessMemory(Core::NONE, Core::READ,
                                  trace_ptr + i * kEntryBytes,
                                  (char*)&entries[i], kEntryBytes,
                                  Core::MEM_MODELED_NONE);
            }
            std::vector<CapturedAccessWire> v(entries, entries + trace_len);
            auto [ins, _] = s_trace_dict.emplace(key, std::move(v));
            entry_ptr = ins->second.data();
            s_dict_misses.fetch_add(1, std::memory_order_relaxed);
         }
      } else {
         for (size_t i = 0; i < trace_len; i++) {
            core->accessMemory(Core::NONE, Core::READ,
                               trace_ptr + i * kEntryBytes,
                               (char*)&entries[i], kEntryBytes,
                               Core::MEM_MODELED_NONE);
         }
         entry_ptr = entries;
      }

      /* Walk the trace.  For each entry, issue core->accessMemory with
         MEM_MODELED_TIME so the cache hierarchy sees the access AND its
         latency is charged to the core's perf model (advances
         barrier.global_time the normal way).  For op=3 (zero_sweep) we
         expand a 4 KiB page into 64 cache-line writes unless the caller
         supplied a static_zero_cycles latency, in which case we issue
         only ONE line-write to populate one line + stretch the timing
         via a shmem-perf advance of the remaining static cost. */
      static thread_local char replay_buf[64];
      uint64_t dep = 0xA5A5A5A5A5A5A5A5ULL ^ cursor;
      uint64_t total_accesses = 0;
      for (size_t i = 0; i < trace_len; i++) {
         const CapturedAccessWire& e = entry_ptr[i];
         uint64_t addr = e.vaddr;
         if (e.dep_flag) {
            addr ^= (dep & 0x3F);  /* low-bits dep-chain; stays in line */
         }
         uint8_t size_log2 = e.size_log2;
         if (size_log2 > 12) size_log2 = 12;

         if (e.op == 3) {
            /* zero_sweep: 4 KiB page init.  Two modes:
               - static_zero_cycles > 0: one real cache-line write
                 (updates one line's cache state) + advance core clock
                 by the remaining static latency via shmem-perf.  This
                 is ~64× cheaper than the full expansion but only
                 models the first line's cache state change.
               - static_zero_cycles == 0: expand to 64 real line writes
                 (accurate cache pressure, slower to simulate). */
            if (static_zero_cycles > 0) {
               core->accessMemory(Core::NONE, Core::WRITE, addr & ~uint64_t(0x3F),
                                  replay_buf, 64, Core::MEM_MODELED_TIME);
               total_accesses++;
               /* Advance shmem clock by the remaining static cost so
                  subsequent accesses are spaced correctly in the cache
                  model.  The clock advance here moves only the shmem
                  model (safe — it's how cache-hit timing is computed);
                  the core's ROB clock catches up via the next real
                  access in the trace. */
               ShmemPerfModel *spm = core->getShmemPerfModel();
               const ComponentPeriod *period = core->getDvfsDomain();
               SubsecondTime now = spm->getElapsedTime(ShmemPerfModel::_USER_THREAD);
               SubsecondTime extra = static_cast<SubsecondTime>(*period) * static_zero_cycles;
               spm->setElapsedTime(ShmemPerfModel::_USER_THREAD, now + extra);
            } else {
               for (uint32_t li = 0; li < 64; li++) {
                  uint64_t line_addr = (addr & ~uint64_t(0xFFF)) + (uint64_t)li * 64;
                  core->accessMemory(Core::NONE, Core::WRITE, line_addr,
                                     replay_buf, 64, Core::MEM_MODELED_TIME);
               }
               total_accesses += 64;
            }
            continue;
         }

         Core::mem_op_t mop;
         switch (e.op) {
            case 0: mop = Core::READ; break;
            case 1: mop = Core::WRITE; break;
            case 2: mop = Core::READ_EX; break;  /* RMW ~ load-exclusive */
            default: mop = Core::READ; break;
         }
         uint32_t size = 1u << size_log2;
         if (size > 64) size = 64;
         core->accessMemory(Core::NONE, mop, addr, replay_buf, size,
                            Core::MEM_MODELED_TIME);
         total_accesses++;

         /* Update dep accumulator so the next dep-chained entry sees a
            plausible "value" dependency.  We can't read the just-issued
            byte back (accessMemory with MEM_MODELED_TIME doesn't return
            the data), so mix the address + cursor in a deterministic
            way.  Sniper's perf model can't see this XOR anyway because
            the whole access happens outside the SIFT stream. */
         dep = (dep << 7) | (dep >> 57);
         dep ^= addr;
      }

      /* Log the first few fires for verification. */
      static std::atomic<uint64_t> s_fast_fault_count{0};
      uint64_t n = s_fast_fault_count.fetch_add(1, std::memory_order_relaxed) + 1;
      if (n <= 8 || (n & (n - 1)) == 0) {
         std::cerr << "[Virtuoso: SimFastFault] #" << n
                   << " core=" << core_id
                   << " fp_id=" << fp_id
                   << " trace_len=" << trace_len
                   << " accesses=" << total_accesses
                   << " static_zero_cyc=" << static_zero_cycles
                   << " cursor=" << cursor
                   << " dict=" << (dict_cache_enabled ? "on" : "off")
                   << " dict_hits=" << s_dict_hits.load(std::memory_order_relaxed)
                   << " dict_misses=" << s_dict_misses.load(std::memory_order_relaxed)
                   << std::endl;
      }
      return 0;
   }
   case SIM_CMD_SET_FAST_FAULT_CHARGE:
   {
      /* Phase 5: set the per-fault latency charge that the MMU should
         queue on the app thread's perf model when userspace MimicOS
         handles a fault.  arg0 = cycles.  Set to the profile's
         mean_cycles right after promotion to TRAINED; reset to 0 if
         the profile is ABANDONED. */
      Sim()->getMimicOS()->setFastFaultChargeCycles(arg0);
      std::cerr << "[Virtuoso: SetFastFaultCharge] cycles=" << arg0 << std::endl;
      return 0;
   }
   case SIM_CMD_MARKER:
   {
      MagicMarkerType args = {thread_id : thread_id, core_id : core_id, arg0 : arg0, arg1 : arg1, str : NULL};
      Sim()->getHooksManager()->callHooks(HookType::HOOK_MAGIC_MARKER, (UInt64)&args);
      return 0;
   }
   case SIM_CMD_START_PROCESS:
   {
      char str[256];
      Core *core = Sim()->getCoreManager()->getCoreFromID(core_id);
      core->accessMemory(Core::NONE, Core::READ, arg0, str, 256, Core::MEM_MODELED_NONE);
      str[255] = '\0';

#if DEBUG_MAGIC_SERVER >= DEBUG_DETAILED
      std::cout << "[Virtuoso: Magic Instruction] Starting process: " << str << std::endl;
#endif
      Sim()->getTraceManager()->createTraceBasedApplication(SubsecondTime::Zero(), str, thread_id);
#if DEBUG_MAGIC_SERVER >= DEBUG_DETAILED
      std::cout << "[Virtuoso: Magic Instruction] Process started" << std::endl;
#endif
      Sim()->getThreadManager()->printInfo();
      return 0;
   }
   case SIM_CMD_USER:
   {
      MagicMarkerType args = {thread_id : thread_id, core_id : core_id, arg0 : arg0, arg1 : arg1, str : NULL};
      return Sim()->getHooksManager()->callHooks(HookType::HOOK_MAGIC_USER, (UInt64)&args, true /* expect return value */);
   }
   case SIM_CMD_INSTRUMENT_MODE:
      return setInstrumentationMode(arg0);
   case SIM_CMD_MHZ_GET:
      return getFrequency(arg0);
   default:
      LOG_ASSERT_ERROR(false, "Got invalid Magic %lu, arg0(%lu) arg1(%lu)", cmd, arg0, arg1);
   }
   return 0;
}

UInt64 MagicServer::getGlobalInstructionCount(void)
{
   UInt64 ninstrs = 0;
   for (UInt32 i = 0; i < Sim()->getConfig()->getApplicationCores(); i++)
      ninstrs += Sim()->getCoreManager()->getCoreFromID(i)->getInstructionCount();
   return ninstrs;
}

static Timer t_start;
UInt64 ninstrs_start;
__attribute__((weak)) void PinDetach(void) {}

void MagicServer::enablePerformance()
{
   Sim()->getStatsManager()->recordStats("roi-begin");
   ninstrs_start = getGlobalInstructionCount();
   t_start.start();

   Simulator::enablePerformanceModels();
   Sim()->setInstrumentationMode(InstMode::inst_mode_roi, true /* update_barrier */);
}

void MagicServer::disablePerformance()
{
   Simulator::disablePerformanceModels();
   Sim()->getStatsManager()->recordStats("roi-end");

   float seconds = t_start.getTime() / 1e9;
   UInt64 ninstrs = getGlobalInstructionCount() - ninstrs_start;
   UInt64 cycles = SubsecondTime::divideRounded(Sim()->getClockSkewMinimizationServer()->getGlobalTime(),
                                                Sim()->getCoreManager()->getCoreFromID(0)->getDvfsDomain()->getPeriod());
   printf("[SNIPER] Simulated %.1fM instructions, %.1fM cycles, %.2f IPC\n",
          ninstrs / 1e6,
          cycles / 1e6,
          float(ninstrs) / (cycles ? cycles : 1));
   printf("[SNIPER] Simulation speed %.1f KIPS (%.1f KIPS / target core - %.1fns/instr)\n",
          ninstrs / seconds / 1e3,
          ninstrs / seconds / 1e3 / Sim()->getConfig()->getApplicationCores(),
          seconds * 1e9 / (float(ninstrs ? ninstrs : 1.) / Sim()->getConfig()->getApplicationCores()));

   PerformanceModel *perf = Sim()->getCoreManager()->getCoreFromID(0)->getPerformanceModel();
   if (perf->getFastforwardPerformanceModel()->getFastforwardedTime() > SubsecondTime::Zero())
   {
      // NOTE: Prints out the non-idle ratio for core 0 only, but it's just indicative anyway
      double ff_ratio = double(perf->getFastforwardPerformanceModel()->getFastforwardedTime().getNS()) / double(perf->getNonIdleElapsedTime().getNS());
      double percent_detailed = 100. * (1. - ff_ratio);
      printf("[SNIPER] Sampling: executed %.2f%% of simulated time in detailed mode\n", percent_detailed);
   }

   fflush(NULL);

   Sim()->setInstrumentationMode(InstMode::inst_mode_end, true /* update_barrier */);
   PinDetach();
}

void print_allocations();

UInt64 MagicServer::setPerformance(bool enabled)
{
   if (m_performance_enabled == enabled)
      return 1;

   m_performance_enabled = enabled;

   // static bool enabled = false;
   static Timer t_start;
   // ScopedLock sl(l_alloc);

   if (m_performance_enabled)
   {
      printf("[SNIPER] Enabling performance models\n");
      fflush(NULL);
      t_start.start();
      logmem_enable(true);
      Sim()->getHooksManager()->callHooks(HookType::HOOK_ROI_BEGIN, 0);
   }
   else
   {
      Sim()->getHooksManager()->callHooks(HookType::HOOK_ROI_END, 0);
      printf("[SNIPER] Disabling performance models\n");
      float seconds = t_start.getTime() / 1e9;
      printf("[SNIPER] Leaving ROI after %.2f seconds\n", seconds);
      fflush(NULL);
      logmem_enable(false);
      logmem_write_allocations();
   }

   if (enabled)
      enablePerformance();
   else
      disablePerformance();

   return 0;
}

/* Per-thread ROI barrier (Apr 15 2026).

   Coordinator announces expected joiner count once; each thread joins or
   leaves from its own context.  Perf model turns on exactly when the last
   expected joiner arrives, and off when the last joiner leaves. */
UInt64 MagicServer::roiExpect(UInt64 n)
{
   std::lock_guard<std::mutex> lk(m_roi_lock);
   /* Allow raising the count (e.g. a late-spawned live app) but not
      shrinking below the already-joined set. */
   if (n < m_roi_joined.size())
   {
      std::cerr << "[MagicServer] SimRoiExpect(" << n << ") rejected: "
                << m_roi_joined.size() << " threads already joined" << std::endl;
      return 1;
   }
   m_roi_expected = n;
   printf("[SNIPER] ROI barrier: expecting %zu threads\n", m_roi_expected);
   fflush(NULL);
   /* Nothing to do on perf model until the last joiner arrives. */
   return 0;
}

UInt64 MagicServer::threadRoiJoin(thread_id_t thread_id)
{
   bool should_enable = false;
   {
      std::lock_guard<std::mutex> lk(m_roi_lock);
      m_roi_joined.insert(thread_id);
      printf("[SNIPER] ROI join: thread %d (%zu/%zu)\n",
             thread_id, m_roi_joined.size(), m_roi_expected);
      fflush(NULL);
      if (m_roi_expected > 0 && m_roi_joined.size() >= m_roi_expected
          && !m_performance_enabled)
      {
         should_enable = true;
      }
   }
   if (should_enable)
   {
      Sim()->getHooksManager()->callHooks(HookType::HOOK_APPLICATION_ROI_BEGIN, 0);
      if (Sim()->getConfig()->getSimulationROI() == Config::ROI_MAGIC)
         setPerformance(true);
      /* Wake any trace cores spinning in SimWaitForRoi. */
      m_roi_ready.store(true, std::memory_order_release);
   }
   return 0;
}

UInt64 MagicServer::threadRoiLeave(thread_id_t thread_id)
{
   bool should_disable = false;
   {
      std::lock_guard<std::mutex> lk(m_roi_lock);
      m_roi_joined.erase(thread_id);
      printf("[SNIPER] ROI leave: thread %d (%zu/%zu)\n",
             thread_id, m_roi_joined.size(), m_roi_expected);
      fflush(NULL);
      if (m_roi_joined.empty() && m_performance_enabled)
      {
         should_disable = true;
      }
   }
   if (should_disable)
   {
      Sim()->getHooksManager()->callHooks(HookType::HOOK_APPLICATION_ROI_END, 0);
      if (Sim()->getConfig()->getSimulationROI() == Config::ROI_MAGIC)
         setPerformance(false);
      /* Clean shutdown: last live app has left ROI.  Only stop
         TraceManager if there are no trace-based apps still running
         (SimRoiStart path).  In mixed mode, let stop-by-icount or
         trace completion handle the final shutdown. */
      if (!m_legacy_roi_active) {
         printf("[SNIPER] Last app left ROI; stopping TraceManager\n");
         fflush(NULL);
         Sim()->getTraceManager()->stop();
      } else {
         printf("[SNIPER] Last live app left ROI (trace apps still active)\n");
         fflush(NULL);
      }
   }
   return 0;
}

UInt64 MagicServer::setFrequency(UInt64 core_number, UInt64 freq_in_mhz)
{
   UInt32 num_cores = Sim()->getConfig()->getApplicationCores();
   UInt64 freq_in_hz;
   if (core_number >= num_cores)
      return 1;
   freq_in_hz = 1000000 * freq_in_mhz;

   printf("[SNIPER] Setting frequency for core %" PRId64 " in DVFS domain %d to %" PRId64 " MHz\n", core_number, Sim()->getDvfsManager()->getCoreDomainId(core_number), freq_in_mhz);

   if (freq_in_hz > 0)
      Sim()->getDvfsManager()->setCoreDomain(core_number, ComponentPeriod::fromFreqHz(freq_in_hz));
   else
   {
      Sim()->getThreadManager()->stallThread_async(core_number, ThreadManager::STALL_BROKEN, SubsecondTime::MaxTime());
      Sim()->getCoreManager()->getCoreFromID(core_number)->setState(Core::BROKEN);
   }

   // First set frequency, then call hooks so hook script can find the new frequency by querying the DVFS manager
   Sim()->getHooksManager()->callHooks(HookType::HOOK_CPUFREQ_CHANGE, core_number);

   return 0;
}

UInt64 MagicServer::getFrequency(UInt64 core_number)
{
   UInt32 num_cores = Sim()->getConfig()->getApplicationCores();
   if (core_number >= num_cores)
      return UINT64_MAX;

   const ComponentPeriod *per = Sim()->getDvfsManager()->getCoreDomain(core_number);
   return per->getPeriodInFreqMHz();
}

UInt64 MagicServer::setInstrumentationMode(UInt64 sim_api_opt)
{
   InstMode::inst_mode_t inst_mode;
   switch (sim_api_opt)
   {
   case SIM_OPT_INSTRUMENT_DETAILED:
      inst_mode = InstMode::DETAILED;
      break;
   case SIM_OPT_INSTRUMENT_WARMUP:
      inst_mode = InstMode::CACHE_ONLY;
      break;
   case SIM_OPT_INSTRUMENT_FASTFORWARD:
      inst_mode = InstMode::FAST_FORWARD;
      break;
   default:
      LOG_PRINT_ERROR("Unexpected magic instrument opt type: %lx.", sim_api_opt);
   }
   Sim()->setInstrumentationMode(inst_mode, true /* update_barrier */);

   return 0;
}
