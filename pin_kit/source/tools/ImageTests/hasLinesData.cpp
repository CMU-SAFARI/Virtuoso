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

/*
 * 1. compile with debug info- IMG_hasLinesData returns true
 * 2. strip debug info from executable(linux)/compile without debug (mac & windows)- IMG_hasLinesData returns false
 */

#include "pin.H"
#include <assert.h>
#include <iostream>
#include <fstream>

KNOB<string> KnobCases(KNOB_MODE_WRITEONCE,    "pintool",
    "state", "debug", "specify test behavior according to state. debug- debug info exist. no_debug - not exist ");

BOOL hasLineInfoUsingGetSourceLocation(IMG img)
{
    string filename;

    for (SEC sec = IMG_SecHead(img); SEC_Valid(sec); sec = SEC_Next(sec)) {

        for (RTN rtn = SEC_RtnHead(sec); RTN_Valid(rtn); rtn = RTN_Next(rtn)) {
            INT32 line = 0;
            RTN_Open(rtn);
            PIN_GetSourceLocation(RTN_Address(rtn),NULL,&line,&filename);
            if(0 != line)
            {
                RTN_Close(rtn);
                return TRUE;
            }
            for (INS ins = RTN_InsHead(rtn); INS_Valid(ins); ins = INS_Next(ins)) {
                line = 0;
                PIN_GetSourceLocation(INS_Address(ins),NULL,&line,&filename);
                if(0 != line)
                {
                    RTN_Close(rtn);
                    return TRUE;
                }
            }
            RTN_Close(rtn);
        }
    }
    return FALSE;
}


VOID ImageLoad(IMG img, VOID *v)
{
    if(!IMG_IsMainExecutable(img)) return;

    if("debug" == KnobCases.Value())
    {
        ASSERTX(TRUE == hasLineInfoUsingGetSourceLocation(img));
        ASSERTX(TRUE == IMG_hasLinesData(img));
    }
    if("no_debug" == KnobCases.Value())
    {
        ASSERTX(FALSE == hasLineInfoUsingGetSourceLocation(img));
        ASSERTX(FALSE == IMG_hasLinesData(img));
    }
}

int main(int argc, char * argv[])
{
    // Initialize symbol processing
    PIN_InitSymbols();

    // Initialize pin
    PIN_Init(argc, argv);
    // Register ImageLoad to be called when an image is loaded
    IMG_AddInstrumentFunction(ImageLoad, 0);
    PIN_StartProgram();
    return -3;
}

