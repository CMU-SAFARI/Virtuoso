/*BEGIN_LEGAL 
BSD License 

Copyright (c) 2015 Intel Corporation. All rights reserved.
 
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

// This file illustrates a minimal PinPlay driver with
// DCFG-generation.

#include "dcfg_pin_api.H"
#include "pinplay.H"
using namespace dcfg_pin_api;

KNOB<BOOL> KnobPinPlayLogger(KNOB_MODE_WRITEONCE, "pintool", "log", "0",
                             "Activate the pinplay logger");

KNOB<BOOL> KnobPinPlayReplayer(KNOB_MODE_WRITEONCE, "pintool", "replay", "0",
                               "Activate the pinplay replayer");
PINPLAY_ENGINE pinplay_engine;

int main(int argc, char* argv[])
{
    if(PIN_Init(argc,argv))
    {
        cerr << "This tool creates a Dynamic Control Flow Graph (DCFG) of "
        "the target application.\n\n";
        cerr << KNOB_BASE::StringKnobSummary() << endl;
        return -1;
    }

    pinplay_engine.Activate(argc, argv, KnobPinPlayLogger, KnobPinPlayReplayer);

    // Enable DCFG if enabling knob was used.
    DCFG_PIN_MANAGER* dcfgMgr = DCFG_PIN_MANAGER::new_manager();
    if (dcfgMgr->dcfg_enable_knob()) {
        dcfgMgr->activate(&pinplay_engine);
    }
    
    PIN_StartProgram();    // Never returns
    return 0;
}
