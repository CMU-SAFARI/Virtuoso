/*BEGIN_LEGAL 
BSD License 

Copyright (c)2013 Intel Corporation. All rights reserved.
 
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
// @ORIGINAL_AUTHORS: Harish Patil 
//

#include <iostream>

#include "pin.H"
#include "instlib.H"

using namespace std; 

#include "bimodal.H"
#include "pinplay.H"

LOCALVAR BIMODAL bimodal;

using namespace INSTLIB; 

LOCALVAR ofstream *outfile;

#define KNOB_LOG_NAME  "log"
#define KNOB_REPLAY_NAME "replay"
#define KNOB_FAMILY "pintool:pinplay-driver"


PINPLAY_ENGINE pinplay_engine;

KNOB_COMMENT pinplay_driver_knob_family(KNOB_FAMILY, "PinPlay Driver Knobs");

KNOB<BOOL>KnobReplayer(KNOB_MODE_WRITEONCE, KNOB_FAMILY,
                       KNOB_REPLAY_NAME, "0", "Replay a pinball");
KNOB<BOOL>KnobLogger(KNOB_MODE_WRITEONCE,  KNOB_FAMILY,
                     KNOB_LOG_NAME, "0", "Create a pinball");

KNOB<UINT64> KnobPhases(KNOB_MODE_WRITEONCE, 
                        "pintool",
                        "phaselen",
                        "100000000", 
                        "Print branch mispredict stats every these many instructions (and also at the end).\n");
KNOB<string>KnobStatFileName(KNOB_MODE_WRITEONCE,  "pintool",
                     "statfile", "bimodal.out", "Name of the branch predictor stats file.");


INT32 Usage()
{
    cerr <<
        "This pin tool is a simple PinPlay-enabled branch predictor \n"
        "\n";

    cerr << KNOB_BASE::StringKnobSummary() << endl;
    return -1;
}

static void threadCreated(THREADID threadIndex, CONTEXT *, INT32 , VOID *)
{
    if (threadIndex > 0)
    {
        cerr << "More than one thread detected. This tool currently works only for single-threaded programs/pinballs." << endl;
        exit(0);
    }
}


int main(int argc, char *argv[])
{
    if( PIN_Init(argc,argv) )
    {
        return Usage();
    }

    outfile = new ofstream(KnobStatFileName.Value().c_str());
    bimodal.Activate(KnobPhases, outfile);
    
    pinplay_engine.Activate(argc, argv, KnobLogger, KnobReplayer);
    if(KnobLogger)
    {
        cout << "Logger basename " << pinplay_engine.LoggerGetBaseName() 
            << endl;
    }
    if(KnobReplayer)
    {
        cout << "Replayer basename " << pinplay_engine.ReplayerGetBaseName() 
            << endl;
    }

    PIN_AddThreadStartFunction(threadCreated, reinterpret_cast<void *>(0));


    PIN_StartProgram();
}
