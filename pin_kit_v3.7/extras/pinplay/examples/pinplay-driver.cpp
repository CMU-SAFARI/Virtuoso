/*BEGIN_LEGAL 
BSD License 

Copyright (c)2014 Intel Corporation. All rights reserved.
 
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
// @ORIGINAL_AUTHOR: Harish Patil 
//

#include "pin.H"
#include "pinplay.H"
#include "isimpoint_inst.H"
#include "instlib.H"
#include "pinplay-debugger-shell.H"
#ifdef SLICING
#include "SliceEngine.H"
#endif

using namespace INSTLIB;

PINPLAY_ENGINE pinplay_engine;
ISIMPOINT isimpoint;
DR_DEBUGGER_SHELL::ICUSTOM_INSTRUMENTOR *
    CreatePinPlayInstrumentor(DR_DEBUGGER_SHELL::ISHELL *);
DR_DEBUGGER_SHELL::ISHELL *shell = NULL; 

KNOB<BOOL> KnobLogger(KNOB_MODE_WRITEONCE, "pintool", "log", "0",
        "Activate the pinplay logger");
KNOB<BOOL> KnobReplayer(KNOB_MODE_WRITEONCE, "pintool", "replay", "0",
        "Activate the pinplay replayer");


#ifdef SLICING
SLICE_ENGINE sliceEngine;
#endif

LOCALFUN INT32 Usage(CHAR *prog)
{
    cerr << "Usage: " << prog << " Args  -- app appargs ..." << endl;
    cerr << "Arguments:" << endl;
    cerr << KNOB_BASE::StringKnobSummary();
    cerr << endl;
    
    return -1;
}

DR_DEBUGGER_SHELL::ICUSTOM_INSTRUMENTOR 
    *CreatePinPlayInstrumentor(DR_DEBUGGER_SHELL::ISHELL *shell)
{
    return new PINPLAY_DEBUGGER_INSTRUMENTOR(shell);
}

int 
main(int argc, char *argv[])
{

    PIN_InitSymbols();
    if( PIN_Init(argc,argv) )
    {
        return Usage(argv[0]);
    }


    pinplay_engine.Activate(argc, argv, KnobLogger, KnobReplayer);

    DEBUG_STATUS debugStatus = PIN_GetDebugStatus();

    if (debugStatus != DEBUG_STATUS_DISABLED) 
    {
        shell = DR_DEBUGGER_SHELL::CreatePinPlayShell();
        DR_DEBUGGER_SHELL::STARTUP_ARGUMENTS args;
        if (KnobReplayer) 
        {
            args._customInstrumentor = CreatePinPlayInstrumentor(shell);
        }
        if (!shell->Enable(args))
            return 1;
    }

#ifdef SLICING
    sliceEngine.Activate(argc, argv, &pinplay_engine);
#endif

    isimpoint.activate(argc, argv);

    PIN_StartProgram();
}
