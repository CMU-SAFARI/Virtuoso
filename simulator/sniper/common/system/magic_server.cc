#include <iostream>

#include "magic_server.h"
#include "sim_api.h"
#include "simulator.h"
#include "thread_manager.h"
#include "logmem.h"
#include "performance_model.h"
#include "fastforward_performance_model.h"
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
      const core_id_t BEEFY_CORE   = 0L;
      const core_id_t WIMPY_CORE   = 1L;
      auto trace_thread = Sim()->getTraceManager()->getTraceThread(0, 0);
      auto thread = trace_thread->getThread();
#if DEBUG_MAGIC_SERVER >= DEBUG_DETAILED
      std::cout << "[Virtuoso: Magic Instruction] Thread with ID =  " << thread->getId()
                << " is currently running on core: " << (thread->getCore()->getId() == 0 ? "beefy" : "wimpy") << std::endl;
#endif
      // PF Handling was done in user-space MimicOS (kernel), so on BEEFY_CORE (in software, as usual)
      assert(thread->getCore()->getId() == BEEFY_CORE);
      // No migration b/w cores needed...

      // Resotre the current Sift Reader to be the App one...
      trace_thread->setCurrentSiftReader(trace_thread->getAppSiftReader());
#if DEBUG_MAGIC_SERVER >= DEBUG_DETAILED
      std::cout << "[Virtuoso: Magic Instruction] Current SiftReader set to APP" << std::endl;
#endif
      return 42;
   }
   case SIM_CMD_RECEIVE_MESSAGE:
   {
#if DEBUG_MAGIC_SERVER >= DEBUG_DETAILED
      std::cout << "[Virtuoso: Magic Instruction] We need to read the message from Sniper's MimicOS" << std::endl;
#endif
      Core *core = Sim()->getCoreManager()->getCoreFromID(core_id);
      MimicOS_NS::Message* message = Sim()->getMimicOS()->getMessage();
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
