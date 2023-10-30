/*BEGIN_LEGAL 
BSD License 

Copyright (c) 2004-2016 Intel Corporation. All rights reserved.
 
Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

Redistributions of source code must retain the above copyright notice,
this list of conditions and the following disclaimer.  Redistributions
in binary form must reproduce the above copyright notice, this list of
conditions and the following disclaimer in the documentation and/or
other materials provided with the distribution.  Neither the name of
the Intel Corporation nor the names of its contributors may be used to
endorse or promote products derived from this software without
specific prior written permission.
 
THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE INTEL OR
ITS CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
END_LEGAL */

//
// @ORIGINAL_AUTHORS: Cristiano Pereira and Harish Patil 
//

#ifndef PINPLAY_H
#define PINPLAY_H

#include "pin.H"
#include "control_manager.H"

#if PIN_PRODUCT_VERSION_MINOR < 8
// added to make latest PinPlay sources to build with older Pin kits
#define INS_IsValidForIpointAfter(ins) INS_HasFallThrough(ins) //FIXME
#define INS_IsValidForIpointTakenBranch(ins) INS_IsBranchOrCall(ins) //FIXME
#define IMG_EntryAddress(img) IMG_Entry(img) //FIXME
#define  INS_IsDirectControlFlow(ins) INS_IsDirectBranchOrCall(ins) //FIXME
#define  INS_IsIndirectControlFlow(ins) INS_IsIndirectBranchOrCall(ins) //FIXME
#define INS_IsIndirectControlFlowViaRegister(ins) INS_CallOrBranchIsRegisterIndirect(ins) //FIXME
#define INS_IsIndirectControlFlowViaMemory(ins) INS_CallOrBranchIsMemoryIndirect(ins) //FIXME
#define INS_GetIndirectControlFlowRegister(ins) INS_CallOrBranchGetIndirectRegister(ins) //FIXME
#define INS_DirectControlFlowTargetAddress(ins) INS_DirectBranchOrCallTargetAddress(ins) //FIXME
#endif



#define INVALID_PINPLAY_TID  ((UINT32)-1)

// Allow user tools to generate messages into the PinPlay message file
// Useful when trying to resolve ordering issues between PinPlay and tool 
// instrumentation...
extern VOID PP_Message(const string &);
class PINPLAY_IMPL;

typedef VOID (*PINPLAY_HANDLER)(VOID*);

typedef VOID (*PINPLAY_SYNC_CALLBACK)(BOOL, THREADID, THREADID, UINT64, VOID*);

class PINPLAY_ENGINE
{
  public:
    PINPLAY_ENGINE();

    // initializes and add instrumentation depending on whether logger, 
    // replayer or both are activated
    VOID Activate(int argc, char *argv[], BOOL activate_logger,
        BOOL activate_replayer);
    static PINPLAY_IMPL * Impl();
    VOID ReplayerDoBreakpoint(CONTEXT * ctxt, THREADID pin_tid, 
        BOOL after, string msg);
    UINT64 LoggerGetICount(THREADID pin_tid);
    UINT64 ReplayerGetICount(THREADID pin_tid);
    string LoggerGetBaseName();
    VOID LoggerSetBaseName(const string & name);
    string ReplayerGetBaseName();
    CONTROLLER::CONTROL_MANAGER *LoggerGetController();
    UINT32 LoggerGetPinPlayTid(THREADID pintid);
    UINT32 ReplayerGetPinPlayTid(THREADID pintid);
    ADDRINT ReplayerTranslateAddress(ADDRINT addr); 

    BOOL IsLoggerActive() { return _logger_active;}
    BOOL IsReplayerActive() { return _replayer_active;}

    BOOL IsLoggerRecording();
    
    //register to start/stop logging callbacks
    VOID RegisterRegionStart(PINPLAY_HANDLER handler,VOID* args);
    VOID RegisterRegionStop(PINPLAY_HANDLER handler,VOID* args);

    //register callback to be called on synchronization delay
    VOID RegisterSyncCallback(PINPLAY_SYNC_CALLBACK cb, VOID* args);
    
    //retrieve region's base name
    string GetRegionBaseName(THREADID tid);

    //generate procinfo XML file for the run based on image load
    VOID GenerateProcInfo(const string &filename, const string &pname);
    VOID FinalizeProcInfo();
    
    // Return OS pid and os tid
    UINT32 GetOSPid();

    static enum CALL_ORDER PinPlayFirstBeforeCallOrder();
    static enum CALL_ORDER PinPlayLastAfterCallOrder();

  private:
    static PINPLAY_IMPL * _impl;
    BOOL _logger_active;
    BOOL _replayer_active;
};
#endif
