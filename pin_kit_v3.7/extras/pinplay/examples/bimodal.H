/*BEGIN_LEGAL 
BSD License 

Copyright (c)2012 Intel Corporation. All rights reserved.
 
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
class BIMODAL
{
  public:
    BIMODAL();
    UINT64 References() { return _references;};
    UINT64 Predicts() { return _predicts;};
    UINT64 Icount() { return _icount;};
    UINT64 Mispredicts() { return _references - _predicts;};
    VOID Activate(UINT64 phaseLen, ofstream *outfile)
    {
        _phaseLen = phaseLen;
        _nextPrintIcount = phaseLen;
        _outfile = outfile;
        INS_AddInstrumentFunction(Instruction, this);
        PIN_AddFiniFunction(PrintStats, this);
    }

  private:
    enum 
    {
        TABLESIZE = 4096
    };

    static VOID Instruction(INS ins, VOID *v);
    static VOID CondBranch(VOID *v, VOID * v2,  BOOL taken);
    static VOID Count(VOID *v);
    static VOID PrintStats(int n, VOID *v);
    INT8 * HistAddress(INS ins);
    ofstream *_outfile;

    INT8 _branchHistory[TABLESIZE];
    UINT64 _references, _predicts, _icount;
    UINT64 _phaseLen, _nextPrintIcount;
};

BIMODAL::BIMODAL()
{
    _references = 0;
    _predicts = 0;
    _icount = 0;
    _phaseLen = 0;
    _nextPrintIcount = 0;
}

VOID BIMODAL::Instruction(INS ins, VOID *v)
{
    BIMODAL *bm = reinterpret_cast<BIMODAL*>(v);
    INS_InsertPredicatedCall(ins, IPOINT_BEFORE, 
           (AFUNPTR)Count, IARG_PTR, (VOID *)bm, IARG_END);
    if ((INS_IsBranch(ins) || INS_IsCall(ins)) && INS_HasFallThrough(ins))
        {
            INS_InsertCall(ins, IPOINT_BEFORE, 
                           (AFUNPTR)CondBranch, IARG_PTR, (VOID *)bm, IARG_PTR, (VOID *)bm->HistAddress(ins), IARG_BRANCH_TAKEN, IARG_END);
        }
}

INT8 * BIMODAL::HistAddress(INS ins)
{
    ADDRINT ip = INS_Address(ins);
    return &(_branchHistory[ip % TABLESIZE]);
}

VOID BIMODAL::CondBranch(VOID *v, VOID * v2, BOOL taken)
{
    BIMODAL *bm = reinterpret_cast<BIMODAL*>(v);
    INT8  *branchHistory = reinterpret_cast<INT8*>(v2);
    INT8 history;

    bm->_references += 1;

    history = (*branchHistory) & 0x3;

    if((taken && history >= 2) || (!taken && history <= 1))
        bm->_predicts += 1;

    INT8 delta = 0;
    if (taken && (history < 3))
    {
        delta = 1;
    }
    if (!taken && (history > 0))
    {
        delta = -1;
    }
  
    *branchHistory += delta;
}

VOID BIMODAL::PrintStats(int n, VOID *v)
{
    BIMODAL *bm = reinterpret_cast<BIMODAL*>(v);
    *(bm->_outfile) << endl;
    *(bm->_outfile) << "Icount: " << dec << bm->Icount() << " Mispredicts: " << dec << bm->Mispredicts() << endl;
}

VOID BIMODAL::Count(VOID *v)
{
    BIMODAL *bm = reinterpret_cast<BIMODAL*>(v);
    bm->_icount++;
    if(bm->_icount > bm->_nextPrintIcount)
    {
        bm->PrintStats(0,bm);
        bm->_nextPrintIcount += bm->_phaseLen; 
    }
}
