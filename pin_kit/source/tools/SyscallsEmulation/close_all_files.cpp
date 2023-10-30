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
/*! @file
* This file together with application and makefile checks that Pin guards the file descriptors opened by itself and the
* tool (including pin.log), and doesn't let the application to close them.
* The tool also checks that standard file descriptors (0-2) cannot be return by PinCRT (for example for open())
*/
#include <stdio.h>
#include <unistd.h>
#include <tool_macros.h>
#include <fstream>
#include <iostream>
#include <fcntl.h>
#include "pin.H"

typedef VOID (*EXITFUNCPTR)(INT code);

KNOB<string> KnobOutputFile(KNOB_MODE_WRITEONCE,        "pintool",
    "o", "close_all_files.out", "specify trace file name");

KNOB<string> KnobForkedChildOutputFile(KNOB_MODE_WRITEONCE,        "pintool",
    "f", "afterForkInChild_tool.log", "forked child tool log");

KNOB<BOOL> KnobToolProbeMode(KNOB_MODE_WRITEONCE, "pintool", "probe", "0",
        "invoke tool in probe mode");


FILE * outc;
ofstream outcpp;
int my_pipe[2];
EXITFUNCPTR origExit;

VOID Fini(INT32 code, VOID *v)
{
    const char* my_str = "MyString";
    char buf[16];
    int res;
    res = (int)write(my_pipe[1], my_str, strlen(my_str));
    ASSERTX(res == (int)strlen(my_str));
    res = close(my_pipe[1]);
    ASSERTX(res == 0);
    res = (int)read(my_pipe[0], buf, sizeof(buf));
    ASSERTX(res == (int)strlen(my_str));
    res = close(my_pipe[0]);
    ASSERTX(res == 0);
    fprintf(outc, "C Success!\n");
    fclose(outc);
    outcpp << "C++ Success!" << endl;
    outcpp.close();
}

/* ===================================================================== */
/* Print Help Message                                                    */
/* ===================================================================== */

INT32 Usage()
{
    PIN_ERROR("This tool practices writing to file descriptors in Fini"
                + KNOB_BASE::StringKnobSummary() + "\n");
    return 1;
}

VOID ExitInProbeMode(INT code)
{
    Fini(code, 0);
    (*origExit)(code);
}

/* ===================================================================== */

VOID ImageLoad(IMG img, VOID *v)
{
    RTN exitRtn = RTN_FindByName(img, C_MANGLE("_exit"));
    if (RTN_Valid(exitRtn) && RTN_IsSafeForProbedReplacement(exitRtn))
    {
        origExit = (EXITFUNCPTR) RTN_ReplaceProbed(exitRtn, AFUNPTR(ExitInProbeMode));
    }
}

void AfterForkInChild()
{
    string forked_child_output = KnobForkedChildOutputFile.Value();
    ADDRINT flags = (O_CREAT|O_WRONLY|O_APPEND|O_TRUNC);

    // Closing standard input and output to make sure the next open() will not return one them.
    // If open() return file descriptor which is one of the standard file descriptors this mean that Pin/Tool logs
    // can reach console which is wrong
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    int fd = open(forked_child_output.c_str(), flags, 00600);
    if (fd > STDERR_FILENO)
    {
        const char* my_str = "success\n";
        int res;
        res = (int)write(fd, my_str, strlen(my_str));
        ASSERTX(res == (int)strlen(my_str));
    }
    assert(fd > 2);
    std::cerr << "AfterForkInChild::hello" << endl;
    close(fd);
}

void AfterForkInChildJit(THREADID threadid, const CONTEXT * ctxt, VOID * v)
{
    AfterForkInChild();
}
void AfterForkInChildProbed(UINT32 childPid, void *data)
{
    AfterForkInChild();
}

/* ===================================================================== */
/* Main                                                                  */
/* ===================================================================== */

int main(int argc, char *argv[])
{
    if (PIN_Init(argc, argv)) return Usage();
    PIN_InitSymbols();

    string c_output = KnobOutputFile.Value() + "_for_c";
    string cpp_output = KnobOutputFile.Value() + "_for_cpp";

    outc = fopen(c_output.c_str(), "w");
    ASSERTX(NULL != outc);

    outcpp.open(cpp_output.c_str(), ofstream::out);
    ASSERTX(outcpp.good());

    int res = pipe(my_pipe);
    ASSERTX(res == 0);

    // Never returns
    if (KnobToolProbeMode)
    {
        IMG_AddInstrumentFunction(ImageLoad, 0);
        PIN_AddForkFunctionProbed(FPOINT_AFTER_IN_CHILD, AfterForkInChildProbed, 0);
        PIN_StartProgramProbed();
    }
    else
    {
        PIN_AddForkFunction(FPOINT_AFTER_IN_CHILD, AfterForkInChildJit, 0);
        PIN_AddFiniFunction(Fini, 0);
        PIN_StartProgram();
    }

    return 1;
}
