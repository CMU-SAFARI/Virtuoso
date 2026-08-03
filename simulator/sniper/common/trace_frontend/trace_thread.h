#ifndef __TRACE_THREAD_H
#define __TRACE_THREAD_H

#include "fixed_types.h"
#include "_thread.h"
#include "thread.h"
#include "core.h"
#include "sift_reader.h"
#include "operand.h"
#include "semaphore.h"

#include <decoder.h>

//extern "C" {
//#include "xed-interface.h"
//}

#include <unordered_map>

#define NUM_PAPI_COUNTERS 6

#define PAPI_TOT_INS 0
#define PAPI_TOT_CYC 1
#define PAPI_L1_DCM 2
#define PAPI_L2_DCM 3
#define PAPI_L3_TCM 4
#define PAPI_BR_MSP 5

class Instruction;
class DynamicInstruction;

class TraceThread : public Runnable
{
   private:
      // In multi-process mode, we want each process to have its own private memory space
      // Therefore, perform a virtual to physical address mapping by including the core_id
      // Virtual addresses are converted to physical addresses by pasting the core_id at
      // bit positions pa_core_shift..+pa_core_size
      // The highest virtual address used is normally 00007fffffffffff, with pa_core_shift==48
      // the core_id is 2 above the highest used bit.
      static const UInt64 pa_core_shift = 48;
      static const UInt64 pa_core_size = 16;
      static const UInt64 pa_va_mask = ~(((UInt64(1) << pa_core_size) - 1) << pa_core_shift);
      // Optionally we can also do address randomization on a per-page basis.
      // This can avoid artificial set contention when replaying multiple copies of the same trace.
      static const UInt64 va_page_shift = 12;
      static const UInt64 va_page_mask = (UInt64(1) << va_page_shift) - 1;

      static UInt64 _va2pa(UInt64 self, UInt64 va) { return ((TraceThread*)self)->va2pa(va); }
      UInt64 va2pa(UInt64 va, bool *noMapping = NULL);
      UInt64 remapAddress(UInt64 va_page);

      _Thread *m__thread;
      Thread *m_thread;
      SubsecondTime m_time_start;

      Sift::Reader m_trace;
      Sift::Reader *m_kernel_trace;          // This is the sift reader for the kernel trace, if any
      Sift::Reader *m_app_trace;             // This is the sift reader for the application trace, if any
      Sift::Reader *m_current_sift_reader;   // This is the sift reader for the trace file, if any

      bool is_in_kernel_mode;

      bool m_trace_has_pa;
      bool m_champsim_trace;

      /* App reader replay (for injected traces only).  When a trace's
         app reader EOFs, a new Reader can be opened on the same file
         to let the TraceThread continue simulation.  Live apps set
         the path to empty — EOF on a live app reader means the live
         app finished and we should stop the simulation. */
      String m_app_trace_path;

      /* Live-app injected-app tracking (Stage 1, Apr 18 2026).  When
         injectForkedApp installs an app reader into a *kernel* pthread's
         TraceThread (m_app_id == 0), the injected live app has its own
         app_id (e.g. 1, 2, ...) that newly-spawned app threads must
         inherit.  handleNewThreadFunc uses this when current==app so a
         pthread_create inside the live app creates a TraceThread in the
         RIGHT application — not in app 0 (the kernel). */
      app_id_t m_injected_app_id = (app_id_t)-1;

      /* Stage 3 (Apr 18 2026): the currently-scheduled app_thread_id on
         this TraceThread (the pool id of whatever reader is in the
         app_reader slot).  Needed at preemption time so we can tell the
         kernel which user thread just got preempted, and so we can stash
         its (inst, next_inst) pair back into the right pool entry for
         resumption on a later SimContextSwitchTo. */
      uint64_t m_running_app_tid = 0;

      /* Stage 4 (Apr 18 2026): marks TraceThreads that represent MimicOS
         kernel pthreads (one per core) rather than dedicated app threads.
         Every kernel pthread is registered under Sniper app_id=0 for
         compatibility with the existing Sniper trace model, so the
         legacy app_id-based "same app = same group" heuristic in
         TraceManager::endApplication wrongly stops unrelated cores when
         ANY live app's SYS_exit_group fires.  This flag lets us exclude
         kernel pthreads from that sweep without a global config check. */
      bool m_is_kernel_pthread = false;

      /* Stage 3 (Apr 18 2026): scheduling quantum in DETAIL-mode app
         instructions.  When m_instrs_this_quantum hits this threshold,
         the run-func calls preemptToKernel() to yield back to the
         kernel for rescheduling.  Default is 10000 instructions; can be
         overridden via the scheduler/quantum_instructions config. */
      uint64_t m_quantum_instrs       = 10000;
      uint64_t m_instrs_this_quantum  = 0;
      UInt32 m_champsim_access_size;
      bool m_address_randomization;
      bool m_appid_from_coreid;
      uint8_t m_address_randomization_table[256];
      bool m_stop;
      std::unordered_map<IntPtr, Instruction *> m_icache;
      //std::unordered_map<IntPtr, const xed_decoded_inst_t *> m_decoder_cache;  // TODO convert to DecoderLib
      //static bool xed_initialized;  // TODO convert to DecoderLib
      //xed_state_t m_xed_state_init;  // TODO convert to DecoderLib
      std::unordered_map<IntPtr, const dl::DecodedInst *> m_decoder_cache;  // TODO convert to DecoderLib
      
      // ChampSim instruction cache: key is (PC, is_branch, num_src_regs, num_dest_regs, num_loads, num_stores)
      // This allows reusing instructions when the same PC has the same operand signature
      struct ChampSimCacheKey {
         IntPtr pc;
         uint8_t is_branch;
         uint8_t num_src_regs;
         uint8_t num_dest_regs;
         uint8_t num_loads;
         uint8_t num_stores;
         
         bool operator==(const ChampSimCacheKey& other) const {
            return pc == other.pc && is_branch == other.is_branch &&
                   num_src_regs == other.num_src_regs && num_dest_regs == other.num_dest_regs &&
                   num_loads == other.num_loads && num_stores == other.num_stores;
         }
      };
      struct ChampSimCacheKeyHash {
         size_t operator()(const ChampSimCacheKey& k) const {
            // Combine all fields into a hash
            return std::hash<IntPtr>()(k.pc) ^ 
                   (std::hash<uint8_t>()(k.is_branch) << 1) ^
                   (std::hash<uint8_t>()(k.num_src_regs) << 2) ^
                   (std::hash<uint8_t>()(k.num_dest_regs) << 3) ^
                   (std::hash<uint8_t>()(k.num_loads) << 4) ^
                   (std::hash<uint8_t>()(k.num_stores) << 5);
         }
      };
      std::unordered_map<ChampSimCacheKey, Instruction*, ChampSimCacheKeyHash> m_champsim_icache;
      
      // ChampSim instruction cache statistics
      UInt64 m_champsim_icache_hits;
      UInt64 m_champsim_icache_misses;
      
      UInt64 m_bbv_base;
      UInt64 m_bbv_count;
      UInt64 m_bbv_last;
      bool m_bbv_end;
      static int m_isa;
      //xed_syntax_enum_t m_syntax;
      uint8_t m_output_leftover[160];
      uint16_t m_output_leftover_size;
      String m_tracefile;
      String m_responsefile;

      String m_tracefile_kernel;
      String m_responsefile_kernel;
      
      app_id_t m_app_id;
      bool m_blocked;
      bool m_cleanup;
      bool m_started;

      int fd_read;
      int fd_write;

      // INVARIANT: to_be_replayed_inst.sinst != NULL iff thread->getCore()->getMemoryManager()->is_page_fault = True
      Sift::Instruction  to_be_replayed_inst;      // This is the instruction that is currently being replayed, if any
      Sift::Instruction  to_be_replayed_next_inst; 

      struct statistics
      {
         SubsecondTime kernel_time;
      } stats;
      // Make run() a function pointer which is initialized in the constructor
      typedef void (TraceThread::*RunFunc)();
      RunFunc m_current_run_func;
      
      void run(){
         (this->*m_current_run_func)();
      }

      void m_run_func_default();
      void m_run_func_with_userpace_mimicos();

      Sift::Mode handleInstructionCountFunc(uint32_t icount);
      void handleCacheOnlyFunc(uint8_t icount, Sift::CacheOnlyType type, uint64_t eip, uint64_t address);
      void handleOutputFunc(uint8_t fd, const uint8_t *data, uint32_t size);
      uint64_t handleSyscallFunc(uint16_t syscall_number, const uint8_t *data, uint32_t size);
      int32_t handleNewThreadFunc();
      int32_t handleForkFunc();
      int32_t handleJoinFunc(int32_t thread);
      uint64_t handleMagicFunc(uint64_t a, uint64_t b, uint64_t c);
      bool handleEmuFunc(Sift::EmuType type, Sift::EmuRequest &req, Sift::EmuReply &res);
      void handleRoutineChangeFunc(Sift::RoutineOpType event, uint64_t eip, uint64_t esp, uint64_t callEip);
      void handleRoutineAnnounceFunc(uint64_t eip, const char *name, const char *imgname, uint64_t offset, uint32_t line, uint32_t column, const char *filename);



      Instruction* decode(Sift::Instruction &inst);
      Instruction* decodeChampsim(Sift::Instruction &inst);
      void handleInstructionWarmup(Sift::Instruction &inst, Sift::Instruction &next_inst, Core *core, bool do_icache_warmup, UInt64 icache_warmup_addr, UInt64 icache_warmup_size);
      void handleChampSimWarmup(Sift::Instruction &inst, Sift::Instruction &next_inst, Core *core);
      void handleInstructionDetailed(Sift::Instruction &inst, Sift::Instruction &next_inst, PerformanceModel *prfmdl);
      void handleChampSimDetailed(Sift::Instruction &inst, Sift::Instruction &next_inst, PerformanceModel *prfmdl);
      //void addDetailedMemoryInfo(DynamicInstruction *dynins, Sift::Instruction &inst, const xed_decoded_inst_t &xed_inst, uint32_t mem_idx, Operand::Direction op_type, bool is_pretetch, PerformanceModel *prfmdl);
      void addDetailedMemoryInfo(DynamicInstruction *dynins, Sift::Instruction &inst, const dl::DecodedInst &decoded_inst, uint32_t mem_idx, Operand::Direction op_type, bool is_pretetch, PerformanceModel *prfmdl);
      void addChampSimMemoryInfo(DynamicInstruction *dynins, UInt64 mem_address, Operand::Direction op_type, bool executed, bool is_prefetch, PerformanceModel *prfmdl);
      void unblock();

      SubsecondTime getCurrentTime() const;
      
      //static dl::Decoder *m_decoder;
      dl::DecoderFactory *m_factory;  // we need a factory here to be able to create instructions of any kind
      //const xed_decoded_inst_t* staticDecode(Sift::Instruction &inst);
      const dl::DecodedInst* staticDecode(Sift::Instruction &inst);

      long long *m_papi_counters;
      bool m_virtuos_app;
      
      Lock m_lock;

   public:

         static Sift::Mode __handleInstructionCountFunc(void* arg, uint32_t icount)
      { return ((TraceThread*)arg)->handleInstructionCountFunc(icount); }
      static void __handleCacheOnlyFunc(void* arg, uint8_t icount, Sift::CacheOnlyType type, uint64_t eip, uint64_t address)
      { ((TraceThread*)arg)->handleCacheOnlyFunc(icount, type, eip, address); }
      static void __handleOutputFunc(void* arg, uint8_t fd, const uint8_t *data, uint32_t size)
      { ((TraceThread*)arg)->handleOutputFunc(fd, data, size); }
      static uint64_t __handleSyscallFunc(void* arg, uint16_t syscall_number, const uint8_t *data, uint32_t size)
      { return ((TraceThread*)arg)->handleSyscallFunc(syscall_number, data, size); }
      static int32_t __handleNewThreadFunc(void* arg)
      { return ((TraceThread*)arg)->handleNewThreadFunc(); }
      static int32_t __handleJoinFunc(void* arg, int32_t join_thread_id)
      { return ((TraceThread*)arg)->handleJoinFunc(join_thread_id); }
      static uint64_t __handleMagicFunc(void* arg, uint64_t a, uint64_t b, uint64_t c)
      { return ((TraceThread*)arg)->handleMagicFunc(a, b, c); }
      static bool __handleEmuFunc(void* arg, Sift::EmuType type, Sift::EmuRequest &req, Sift::EmuReply &res)
      { return ((TraceThread*)arg)->handleEmuFunc(type, req, res); }
      static void __handleRoutineChangeFunc(void* arg, Sift::RoutineOpType event, uint64_t eip, uint64_t esp, uint64_t callEip)
      { ((TraceThread*)arg)->handleRoutineChangeFunc(event, eip, esp, callEip); }
      static void __handleRoutineAnnounceFunc(void* arg, uint64_t eip, const char *name, const char *imgname, uint64_t offset, uint32_t line, uint32_t column, const char *filename)
      { ((TraceThread*)arg)->handleRoutineAnnounceFunc(eip, name, imgname, offset, line, column, filename); }
      static int32_t __handleForkFunc(void* arg)
      { return ((TraceThread*)arg)->handleForkFunc();}

      
      bool m_stopped;

      TraceThread(Thread *thread, SubsecondTime time_start, String tracefile, String responsefile, app_id_t app_id, bool cleanup);
      ~TraceThread();

      void spawn();
      void stop() { m_stop = true; }
      void cleanupChampSimCache();  // Clean up ChampSim instruction cache to avoid leaks
      UInt64 getProgressExpect();
      UInt64 getProgressValue();
      void frontEndStop(); //Ask all trace_threads to send signal to front-end to shutdown

      Thread* getThread() const { return m_thread; }
      bool getVirtuosApp() { return m_virtuos_app; }
     
      void handleAccessMemory(Core::lock_signal_t lock_signal, Core::mem_op_t mem_op_type, IntPtr d_addr, char* data_buffer, UInt32 data_size);
      
      Sift::Reader* getSiftReader() { return &m_trace; }
      
      Sift::Reader* getKernelSiftReader() { return m_kernel_trace; }
      void setKernelSIFTRreader(Sift::Reader *reader) { m_kernel_trace = reader; }
         
      Sift::Reader* getAppSiftReader() { return m_app_trace; }
      void setAppSiftReader(Sift::Reader *reader) { m_app_trace = reader; }

      Sift::Reader* getCurrentSiftReader() { return m_current_sift_reader; }
      void setCurrentSiftReader(Sift::Reader *reader) { m_current_sift_reader = reader; }

      /* Mark the app reader as replayable (trace) by storing its file path.
         Empty path = non-replayable (live app). */
      void setAppTracePath(const String& path) { m_app_trace_path = path; }
      const String& getAppTracePath() const { return m_app_trace_path; }

      /* The app_id of the live app whose reader is currently injected into
         THIS kernel pthread's TraceThread.  (app_id_t)-1 means "no live app
         injected — this TraceThread is either a plain kernel pthread or
         has only a trace-file app reader." */
      void setInjectedAppId(app_id_t id) { m_injected_app_id = id; }
      app_id_t getInjectedAppId() const { return m_injected_app_id; }

      void setIsKernelPthread(bool v) { m_is_kernel_pthread = v; }
      bool isKernelPthread() const { return m_is_kernel_pthread; }

      /* Stage 3 (Apr 18 2026): which pool id the TraceThread is currently
         running.  Updated by the SimContextSwitchTo handler. */
      void setRunningAppTid(uint64_t atid) {
         m_running_app_tid = atid;
         m_instrs_this_quantum = 0;
      }
      uint64_t getRunningAppTid() const { return m_running_app_tid; }

      /* Stage 4 (Apr 18 2026): app-thread EOF transition.
         Called from the mimicos run-func when the currently-scheduled
         app reader returns EOF and is not replayable (a live app has
         exited).  Posts a thread_exit event to this core's kernel,
         unregisters the atid from AppReaderPool, clears
         m_running_app_tid, swaps current_reader back to kernel_reader,
         and sends a response-after-context-switch to wake the kernel
         pthread.  Caller should `continue` the outer while-loop so the
         next Read comes from the kernel pipe — the kernel will process
         thread_exit (popping the runqueue) and decide what to schedule
         next (if anything).  If the runqueue has more work, the kernel
         issues SimContextSwitchTo and we flip back to app mode; if not,
         this TraceThread stays in kernel mode indefinitely (or until
         a new_thread event arrives from another core, or simulation
         shutdown). */
      void notifyAppExitAndSwitchToKernel();

      /* Stage 3 (Apr 18 2026): quantum expired — yield back to the kernel.
         Saves current app (inst,next_inst) to the pool so the next
         SimContextSwitchTo to the same id resumes correctly, posts a
         "quantum_expired" event to this core's kernel message slot,
         switches current_reader to kernel_reader, and sends a response-
         after-context-switch to unblock the PIN-side kernel Writer.
         Caller must `continue` the outer loop after invoking this so the
         next Read is from the kernel pipe. */
      void preemptToKernel(const Sift::Instruction& inst_app,
                           const Sift::Instruction& next_inst_app);

      /* Stage 3 (Apr 18 2026): setter for resumption-after-preemption.
         SimContextSwitchTo handler uses this to restore a previously-
         preempted thread's (inst, next_inst) pair onto the to-be-replayed
         slots, so the mimicos run-func's existing fault-replay branch
         picks them up on the next iteration. */
      void setToBeReplayed(const Sift::Instruction& inst,
                           const Sift::Instruction& next_inst) {
         to_be_replayed_inst      = inst;
         to_be_replayed_next_inst = next_inst;
      }

      /* Called when the app reader returns EOF.  If the app is a replayable
         trace (m_app_trace_path non-empty), opens a new Reader on the same
         file, installs it as the app reader, and returns true.  Otherwise
         returns false (caller should exit the loop). */
      bool tryReplayAppReader();

      /* Convenience for mimicos run-func: on EOF of app reader, replay
         and read the (inst, next_inst) pair from the start of the trace.
         Returns false if not replayable or if the fresh Reader fails. */
      bool replayAndReadPair(Sift::Instruction &inst, Sift::Instruction &next_inst);
};

#endif // __TRACE_THREAD_H
