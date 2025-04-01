#include "globals.h"

KNOB<std::string> KnobOutputFile(KNOB_MODE_WRITEONCE, "pintool", "o", "trace", "output");
KNOB<UINT64> KnobBlocksize(KNOB_MODE_WRITEONCE, "pintool", "b", "0", "blocksize");
KNOB<UINT64> KnobUseROI(KNOB_MODE_WRITEONCE, "pintool", "roi", "0", "use ROI markers");
KNOB<UINT64>
KnobMPIImplicitROI(KNOB_MODE_WRITEONCE, "pintool", "roi-mpi", "0", "Implicit ROI between MPI_Init and MPI_Finalize");
KNOB<UINT64> KnobFastForwardTarget(KNOB_MODE_WRITEONCE, "pintool", "f", "0", "instructions to fast forward");
KNOB<UINT64>
KnobDetailedTarget(KNOB_MODE_WRITEONCE, "pintool", "d", "0", "instructions to trace in detail (default = all)");
KNOB<UINT64> KnobUseResponseFiles(
    KNOB_MODE_WRITEONCE,
    "pintool",
    "r",
    "0",
    "use response files (required for multithreaded applications or when emulating syscalls, default = 0)");
KNOB<UINT64> KnobEmulateSyscalls(KNOB_MODE_WRITEONCE,
                                 "pintool",
                                 "e",
                                 "0",
                                 "emulate syscalls (required for multithreaded applications, default = 0)");
KNOB<BOOL>
KnobSendPhysicalAddresses(KNOB_MODE_WRITEONCE, "pintool", "pa", "0", "send logical to physical address mapping");
KNOB<UINT64>
KnobFlowControl(KNOB_MODE_WRITEONCE, "pintool", "flow", "1000", "number of instructions to send before syncing up");
KNOB<UINT64>
KnobFlowControlFF(KNOB_MODE_WRITEONCE,
                  "pintool",
                  "flowff",
                  "100000",
                  "number of instructions to batch up before sending instruction counts in fast-forward mode");
KNOB<INT64> KnobSiftAppId(KNOB_MODE_WRITEONCE, "pintool", "s", "0", "sift app id (default = 0)");
KNOB<BOOL> KnobRoutineTracing(KNOB_MODE_WRITEONCE, "pintool", "rtntrace", "0", "routine tracing");
KNOB<BOOL>
KnobRoutineTracingOutsideDetailed(KNOB_MODE_WRITEONCE, "pintool", "rtntrace_outsidedetail", "0", "routine tracing");
KNOB<BOOL> KnobDebug(KNOB_MODE_WRITEONCE, "pintool", "debug", "0", "start debugger on internal exception");
KNOB<BOOL> KnobVerbose(KNOB_MODE_WRITEONCE, "pintool", "verbose", "0", "verbose output");
KNOB<UINT64> KnobStopAddress(KNOB_MODE_WRITEONCE, "pintool", "stop", "0", "stop address (0 = disabled)");

KNOB_COMMENT pinplay_driver_knob_family(KNOB_FAMILY, "PinPlay SIFT Recorder Knobs");
KNOB<BOOL> KnobReplayer(KNOB_MODE_WRITEONCE, KNOB_FAMILY, KNOB_REPLAY_NAME, "0", "Replay a pinball");

KNOB<UINT64> KnobExtraePreLoaded(KNOB_MODE_WRITEONCE, "pintool", "extrae", "0", "Extrae preloaded");

#ifdef PINPLAY
PINPLAY_ENGINE pinplay_engine;
#endif /* PINPLAY */

// INT32 app_id;
// INT32 num_threads = 0;
// UINT64 blocksize;
// UINT64 fast_forward_target = 0;
// UINT64 detailed_target = 0;
// PIN_LOCK access_memory_lock;
// PIN_LOCK new_threadid_lock;
// std::deque<ADDRINT> tidptrs;
// INT32 child_app_id = -1;
// BOOL in_roi = false;
// BOOL any_thread_in_detail = false;
// Sift::Mode current_mode = Sift::ModeIcount;
// std::unordered_map<ADDRINT, bool> routines;

extrae_image_t extrae_image;
