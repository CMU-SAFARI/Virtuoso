/*BEGIN_LEGAL 
Intel Open Source License 

Copyright (c) 2002-2018 Intel Corporation. All rights reserved.
 
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
/*!@ file
 * Testing TRACE_Address ( ) API
 * Finds tstfunc1,tstfunc2,tstfunc3 by name and makes sure their addresses match a trace address
 *
 * Test app prints addresses of function pointers, of these 3 functions. Comparison is done in make rule.
 *
 * This test tests TRACE_Address only for traces at the beginning of a function.
 * It does not check traces that begin in the middle of a function.
 */


#include <cassert>
#include <cstdio>
#include <iostream>
#include <fstream>
#include <map>
#include <unistd.h>
#include "pin.H"
#include <cstdlib>

//=========================================================
//Global Variables:

//This map holds all Routine Addresses of routines that include "tstfunc" in their name,
//and a boolean value stating if it was reached by Trace_Address.
std::map<ADDRINT,bool> rtnAdds;

std::ofstream TraceFile;
//=========================================================
//instrumentation functions:

VOID Image(IMG img, VOID *v)
{
    for( SEC sec= IMG_SecHead(img); SEC_Valid(sec); sec = SEC_Next(sec) )
    {
        for( RTN rtn= SEC_RtnHead(sec); RTN_Valid(rtn); rtn = RTN_Next(rtn) )
        {
            if(string::npos!=RTN_Name(rtn).find("tstfunc"))
            {
                rtnAdds.insert(pair<ADDRINT,bool>(RTN_Address(rtn),false));
            }
        }
    }

}

VOID InstrumentTrace (TRACE trace, VOID *v)
{
    ADDRINT addr=TRACE_Address(trace);
    assert(BBL_Address(TRACE_BblHead(trace))==addr);
    if(rtnAdds.find(addr)!=rtnAdds.end())
    {
        rtnAdds.find(addr)->second=true;
    }

}

VOID Fini(INT32 code, VOID *v)
{
    for(std::map<ADDRINT,bool>::iterator it=rtnAdds.begin(); it!=rtnAdds.end(); it++)
    {
        assert(it->second==true);
        char toprint[50];
        sprintf(toprint,"0x%x\t", (unsigned int)it->first);
        TraceFile<<toprint;
    }

    TraceFile.close();
}


/* ===================================================================== */
/* Commandline Switches */
/* ===================================================================== */

KNOB<string> KnobOutputFile(KNOB_MODE_WRITEONCE, "pintool",
    "o", "traceaddress.out", "specify trace file name");

/* ===================================================================== */

//==========================================================
int main(int argc, char * argv[])
{
    PIN_InitSymbols();
    PIN_Init(argc, argv);


    TraceFile.open(KnobOutputFile.Value().c_str());
    TraceFile << hex;
    TraceFile.setf(ios::showbase);

    IMG_AddInstrumentFunction(Image, 0);

    TRACE_AddInstrumentFunction(InstrumentTrace, 0);
    PIN_AddFiniFunction(Fini, 0);

    PIN_StartProgram();

    return 0;
}
