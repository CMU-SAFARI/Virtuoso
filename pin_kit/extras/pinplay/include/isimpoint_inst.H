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

#ifndef ISIMPOINT_INST_H
#define ISIMPOINT_INST_H

#include <map>
#include <set>
#include <queue>
#include <iostream>
#include <fstream>
#include <string.h>
#include <math.h>
#include <unistd.h>

#include "pin.H"
#include "atomic.hpp"
#include "instlib.H"
#include "reuse_distance.H"

#if defined(EMX_INIT)
#include "emu.H"
#endif

#define ISIMPOINT_MAX_IMAGES 64
#define ADDRESS64_MASK (~63)

/*
    When to emit frequency (bbv/ldv) vectors?
    Two knobs control this:
    1. -emit_vectors
        Default 1.
        If set to 0, some external tool (such as looppoint) will trigger
         vector emission.
    2. -delay_vector_emission
        Added to ease marker management in PC+Count regions. 
          (Iregions behavior un-affected.)
        Default 1 :
            Emission happen at the beginning of the next basic block
            following the end of slice is detected.
        If set to 0, we have the old way of outputting vectors as soon
         as slice end is detected.
*/
class IMG_INFO
{
    public:
    IMG_INFO(IMG img);
        INT32 Id() { return _imgId;}
        CHAR * Name() { return _name;}
        ADDRINT  LowAddress() { return _low_address;}
    private:
        CHAR * _name; 
        ADDRINT _low_address; 
        INT32 _imgId;
};

class IMG_MANAGER
{
  private:
    std::map<INT32, IMG_INFO*> _img_info;
    PIN_LOCK              _imagesLock; 

  public:
    IMG_MANAGER() 
    {
        PIN_InitLock(&_imagesLock);
    }

    VOID AddImage(IMG img)
    {
        PIN_GetLock(&_imagesLock, 1);
        _img_info[IMG_Id(img)] = new IMG_INFO(img);
        PIN_ReleaseLock(&_imagesLock);
    }
    IMG_INFO * GetImageInfo(INT32 id)
    {
        IMG_INFO * imageInfo = NULL;
        PIN_GetLock(&_imagesLock, 1);
        if (_img_info.find(id) != _img_info.end())
            imageInfo = _img_info[id];
        PIN_ReleaseLock(&_imagesLock);
        return imageInfo;
    }
};

class BLOCK_KEY
{
    friend BOOL operator<(const BLOCK_KEY & p1, const BLOCK_KEY & p2);
        
  public:
    BLOCK_KEY(ADDRINT s, ADDRINT e, USIZE z) : _start(s),_end(e),_size(z) {};
    BOOL IsPoint() const { return (_start - _end) == 1;  }
    ADDRINT Start() const { return _start; }
    ADDRINT End() const { return _end; }
    USIZE Size() const { return _size; }
    BOOL Contains(ADDRINT addr) const;
    
  private:
    const ADDRINT _start;
    const ADDRINT _end;
    const USIZE _size;
};

// a 64-bit(8 byte) counter with 56 bytes of padding so the entire struct fits
// in a 64-byte cache line on x86
struct GLOBAL_COUNTER64 {
    INT64 _count;
    UINT8 _pad[56];
};

// a 32-bit(4 byte) counter with 60 bytes of padding so the entire struct fits
// in a 64-byte cache line on x86
struct GLOBAL_COUNTER32 {
    INT32 _count;
    UINT8 _pad[60];
};

class PROFILE;
class ISIMPOINT;

LOCALTYPE typedef std::map<INT32, INT64> BLOCK_COUNT_MAP;
LOCALTYPE typedef std::map<INT32, GLOBAL_COUNTER64> BLOCK_COUNT_MAP_GLOBAL;
LOCALTYPE typedef enum 
    { 
           LDV_TYPE_NONE = 0,
           LDV_TYPE_APPROXIMATE = 1,
           LDV_TYPE_EXACT = 2
    }LDV_TYPE;

class LDV
{
  private:
    static const UINT32 MAX_BINS = 30;
    RD *_rd;
    std::vector<UINT64> _counts;
  public:
    LDV(LDV_TYPE type)
        : _counts(MAX_BINS+1, 0)
    {
        if (type == LDV_TYPE_APPROXIMATE)
            _rd = new RD_LogRR();
        else if (type == LDV_TYPE_EXACT)
            _rd = new RD_Treap();
        else
            _rd = NULL;
    }
    ~LDV()
    {
        if (_rd)
            delete _rd;
    }
    VOID emit(std::ofstream &BbFile)
    {
        for(UINT64 bin = 0; bin <= MAX_BINS; ++bin)
        {
            UINT64 value = _counts[bin];
            if (value)
                BbFile << ":" << std::dec << bin << ":" << std::dec << value << " ";
            _counts[bin] = 0;
        }
    }

    VOID access(ADDRINT address)
    {
        ASSERTX(_rd);
        UINT32 dist_log2 = _rd->reference(address);
        if (dist_log2 > MAX_BINS)
            dist_log2 = MAX_BINS;
        ++_counts[dist_log2];
    }
};

class BLOCK
{
  public:
    BLOCK(const BLOCK_KEY & key, INT32 instructionCount, INT32 id, INT32 imgId);
    INT32 StaticInstructionCount() const { return _staticInstructionCount; }
    VOID Execute(THREADID tid) { _sliceBlockCount[tid]++; }
    VOID ExecuteGlobal() 
      {ATOMIC::OPS::Increment<INT32>(&_sliceBlockCountGlobal._count, 1); }
    VOID Execute(THREADID tid, const BLOCK* prev_block, ISIMPOINT *isimpoint);
    VOID ExecuteGlobal(const BLOCK* prev_block, ISIMPOINT *isimpoint);
    VOID EmitSliceEnd(THREADID tid, PROFILE *profile);
    VOID EmitSliceEndGlobal(PROFILE *profile);
    VOID EmitProgramEnd(const BLOCK_KEY & key, THREADID tid,
        PROFILE * profile, const ISIMPOINT *isimpoint) const;
    VOID EmitProgramEndGlobal(const BLOCK_KEY & key, 
        PROFILE * profile, const ISIMPOINT *isimpoint) const;
    INT64 CumulativeBlockCount(THREADID tid) const 
        { return _cumulativeBlockCount[tid] + _sliceBlockCount[tid]; }
    INT64 CumulativeBlockCountGlobal() const 
        { return _cumulativeBlockCountGlobal._count + _sliceBlockCountGlobal._count; }
    UINT32 ImgId() const { return _imgId; }
    const BLOCK_KEY & Key() const { return _key; }
    INT32 Id() const { return _id; }
    
  private:
    INT32 SliceInstructionCount(THREADID tid) const 
        { return _sliceBlockCount[tid] * _staticInstructionCount; }
    INT32 SliceInstructionCountGlobal() const 
        { return _sliceBlockCountGlobal._count * _staticInstructionCount; }

    const INT32 _staticInstructionCount; // number of instrs in this block.
    INT32 _id;
    const BLOCK_KEY _key;

    INT32 _sliceBlockCount[PIN_MAX_THREADS]; 
    GLOBAL_COUNTER32 _sliceBlockCountGlobal; 
    // times this block was executed in the current slice.
    INT64 _cumulativeBlockCount[PIN_MAX_THREADS]; 
    GLOBAL_COUNTER64 _cumulativeBlockCountGlobal; 
    // times this block was executed prior to the current slice.
    UINT32 _imgId;
    BLOCK_COUNT_MAP _blockCountMap[PIN_MAX_THREADS]; 
    BLOCK_COUNT_MAP_GLOBAL _blockCountMapGlobal; 
    // counter for each previous block.
};

LOCALTYPE typedef std::pair<BLOCK_KEY, BLOCK*> BLOCK_PAIR;
LOCALTYPE typedef std::map<BLOCK_KEY, BLOCK*> BLOCK_MAP;

LOCALTYPE typedef std::queue<UINT64> REGION_LENGTHS_QUEUE;
    
class PROFILE
{
    private:
    static const UINT32 BUFSIZE=200;

    public: 
    PROFILE(INT64 slice_size, LDV_TYPE ldv_type)
        : _ldvState(ldv_type)
    {
        first = true;
        last = false;
        active = false;
        first_eip = 0;
        first_eip_imgID = 0;
        CumulativeInstructionCount = 0;
        SliceTimer = slice_size; // may be updated with "-length lfile"
        SliceTimerGlobal._count = slice_size; 
        CurrentSliceSize = slice_size;// may be updated with "-length lfile"
        CurrentSliceSizeGlobal._count = slice_size;
        last_block = NULL;
    }

    VOID OpenFile(THREADID tid, UINT32 pid, std::string output_file, BOOL enable_ldv)
    {
        if ( !BbFile.is_open() )
        {
            char num[100];
            if (pid)
            {
                sprintf(num, ".T.%u.%d", (unsigned)pid, (int)tid);
            }
            else
            {
                sprintf(num, ".T.%d", (int)tid);
            }
            std::string tname = num;
            BbFile.open((output_file+tname+".bb").c_str());
            BbFile.setf(std::ios::showbase);

            if (enable_ldv)
            {
               LdvFile.open((output_file+tname+".ldv").c_str());
            }
        }
    }

    // This the global version
    VOID OpenFileGlobal(UINT32 pid, std::string output_file, BOOL enable_ldv)
    {
        if ( !BbFile.is_open() )
        {
            char gnum[500];
            if (pid)
            {
                sprintf(gnum, ".global.%u", (unsigned)pid);
            }
            else
            {
                sprintf(gnum, ".global");
            }
            std::string tname = gnum;
            BbFile.open((output_file+tname+".bb").c_str());
            BbFile.setf(std::ios::showbase);

            if (enable_ldv)
            {
               LdvFile.open((output_file+tname+".ldv").c_str());
            }
        }
    }

    VOID ReadLengthFile(THREADID tid, std::string length_file)
    {
        std::ifstream lfile(length_file.c_str());
        ASSERT(lfile.is_open(), "Could not open length file:"+length_file);
        UINT32 lineNum = 0;
        UINT32 recordNum = 0;
        while(true)
        {
            if( lfile.eof() )
            {
                break;
            }

            CHAR record[BUFSIZE+1];
            std::string field;

            lfile.getline(record, BUFSIZE);
            lineNum++;

            if(strlen(record)==0) continue;

            // first letter '#' : this is a comment 
            if(record[0]=='#') continue;

            std::istringstream s(record);

            field.clear();
            getline(s, field);
            ASSERT(!field.empty(), "Empty length field.");
            if(recordNum == 0)
            {
                CurrentSliceSize = SliceTimer 
                    = (INT64) Uint64FromString(field);
                // cerr << "First SliceTimer " << SliceTimer << endl;
            }
            else
            {
                length_queue.push(Uint64FromString(field));
            }
            recordNum++;
        }
        lfile.close();
    }
    VOID ExecuteMemory(ADDRINT address) 
        { _ldvState.access (address & ADDRESS64_MASK); }
    VOID EmitLDV() { _ldvState.emit(LdvFile); }

    VOID ExecuteMemoryGlobal(ADDRINT address) 
        {  ASSERT(0,"ExecuteMemoryGlobal() NYT "); }
    VOID EmitLDVGlobal()
        {  ASSERT(0,"EmitLDVGlobal() NYT "); }

    std::ofstream BbFile;
    std::ofstream LdvFile;
    INT64 CumulativeInstructionCount;
    GLOBAL_COUNTER64 CumulativeInstructionCountGlobal;
    // The first time, we want a marker, but no T vector
    ADDRINT first_eip;
    UINT32 first_eip_imgID;
    BOOL first;
    BOOL last;
    BOOL active;
    // Emit the first marker immediately
    INT64 SliceTimer;
    INT64 CurrentSliceSize;
    GLOBAL_COUNTER64 SliceTimerGlobal;
    GLOBAL_COUNTER64 CurrentSliceSizeGlobal;
    BLOCK *last_block;
    LDV _ldvState;
    REGION_LENGTHS_QUEUE length_queue;
};

class ISIMPOINT
{
    BLOCK_MAP block_map;
    std::string commandLine;    
    UINT32 Pid;
    PROFILE ** profiles;
    PROFILE * globalProfile;
    IMG_MANAGER img_manager;
    // If KnobEmitPrevBlockCounts is enabled, this array is used 
    // to assign an ID to each block as it is executed.
    // Otherwise, the ids are assigned at instrumentation time and only 
    // the first entry in the vector is used,
    // since we don't know the thread id at instrumentation. Assigning 
    // at instrumentation time is more efficient
    // if one does not care for the ID assignment order.
    THREADID _currentId[PIN_MAX_THREADS];

    // Flags to let an external friendly class know that frequency vector 
    // needs to be emitted
    BOOL _vectorPending[PIN_MAX_THREADS];

    BOOL _vectorPendingGlobal;

    // The start addresses of the slices
    // Needed for writing the block of the last slice
    std::set<ADDRINT> _slices_start_set;
    PIN_LOCK     _slicesLock; 
    PIN_LOCK     _globalProfileLock; 

  public:
    ISIMPOINT();

    INT32 Usage()
    {
        std::cerr <<
            "This tool collects frequency vectors  for SimPoint.\n"
            "\n";
        std::cerr << KNOB_BASE::StringKnobSummary() << std::endl;
        return -1;
    }

    BOOL VectorPending(THREADID tid)
      { return _vectorPending[tid]; }

    BOOL VectorPendingGlobal()
      { return _vectorPendingGlobal; }

    VOID EmitSliceStartInfo(ADDRINT endMarker, INT64 markerCount, UINT32 imgId, THREADID tid)
    {
        PIN_GetLock(&_slicesLock, 1);
        _slices_start_set.insert(endMarker);
        PIN_ReleaseLock(&_slicesLock);

        IMG_INFO *img_info = img_manager.GetImageInfo(imgId);
        if(!img_info)
        {
            profiles[tid]->BbFile << "M: " << std::hex << endMarker << " " <<
                std::dec << markerCount << " " << "no_image" << " " 
                << std::hex << 0 << std::endl;
            return;
        }
        profiles[tid]->BbFile << "S: " << std::hex << endMarker << " " <<
            std::dec << markerCount << " " << img_info->Name() << " " <<
            std::hex  <<img_info->LowAddress() << " + "; 
        profiles[tid]->BbFile << std::hex << endMarker-img_info->LowAddress(); 
        INT32 lineNumber;
        std::string fileName;
        PIN_LockClient();
        PIN_GetSourceLocation(endMarker, NULL, &lineNumber, &fileName);
        PIN_UnlockClient();
        if(lineNumber)
        {
            profiles[tid]->BbFile  << " # " << fileName << std::dec <<
            ":" << lineNumber << std::endl;
        }
        else
        {
            profiles[tid]->BbFile  << " # Unknown:0" << std::endl;
        }
    }
    
    VOID EmitSliceStartInfoGlobal(ADDRINT endMarker, INT64 markerCount, UINT32 imgId)
    {
        PIN_GetLock(&_slicesLock, 1);
        _slices_start_set.insert(endMarker);
        PIN_ReleaseLock(&_slicesLock);

        IMG_INFO *img_info = img_manager.GetImageInfo(imgId);
        if(!img_info)
        {
            globalProfile->BbFile << "M: " << std::hex << endMarker << " " <<
                std::dec << markerCount << " " << "no_image" << " " 
                << std::hex << 0 << std::endl;
            return;
        }
        globalProfile->BbFile << "S: " << std::hex << endMarker << " " <<
            std::dec << markerCount << " " << img_info->Name() << " " <<
            std::hex  <<img_info->LowAddress() << " + "; 
        globalProfile->BbFile << std::hex << endMarker-img_info->LowAddress(); 
        INT32 lineNumber;
        std::string fileName;
        PIN_LockClient();
        PIN_GetSourceLocation(endMarker, NULL, &lineNumber, &fileName);
        PIN_UnlockClient();
        if(lineNumber)
        {
            globalProfile->BbFile  << " # " << fileName << std::dec <<
            ":" << lineNumber << std::endl;
        }
        else
        {
            globalProfile->BbFile  << " # Unknown:0" << std::endl;
        }
    }
    
    VOID EmitSliceEnd(ADDRINT endMarker, UINT32 imgId, THREADID tid, 
            UINT32 markerCountOffset=0)
    {
        
        INT64 markerCount = markerCountOffset;
        
        if (profiles[tid]->first == true)
        {
            // Input merging will change the name of the input
            profiles[tid]->BbFile << "I: 0" << std::endl;
            profiles[tid]->BbFile << "P: " << std::dec << tid << std::endl;
            profiles[tid]->BbFile << "C: sum:dummy Command:" 
                << commandLine << std::endl;
            EmitSliceStartInfo(profiles[tid]->first_eip, 1,
                     profiles[tid]->first_eip_imgID, tid);        
        }
        
        profiles[tid]->BbFile << "# Slice ending at " << std::dec 
            << profiles[tid]->CumulativeInstructionCount << std::endl;
        
        if ( !profiles[tid]->first || KnobEmitFirstSlice )
            profiles[tid]->BbFile << "T" ;

        for (BLOCK_MAP::const_iterator bi = block_map.begin(); 
            bi != block_map.end(); bi++)
        {
            BLOCK * block = bi->second;
            const BLOCK_KEY & key = bi->first;
            
            if (key.Contains(endMarker))
            {
                markerCount += block->CumulativeBlockCount(tid);
            }
            
            if ( !profiles[tid]->first || KnobEmitFirstSlice )
                block->EmitSliceEnd(tid, profiles[tid]);
        }

        if ( !profiles[tid]->first || KnobEmitFirstSlice )
            profiles[tid]->BbFile << std::endl;

        if (_ldv_type != LDV_TYPE_NONE )
        {
            if ( !profiles[tid]->first || KnobEmitFirstSlice )
            {
                profiles[tid]->LdvFile << "T" ;
                profiles[tid]->EmitLDV();
                profiles[tid]->LdvFile << std::endl;
            }
        }

        if ( profiles[tid]->active  && !profiles[tid]->last)
        {
        // This is the start marker for the next slice (hence skipping for 'last') 
            if (KnobNoSymbolic)
            {
                profiles[tid]->BbFile << "M: " << std::hex << endMarker 
                    << " " << std::dec << markerCount << std::endl;
            }
            else
            {
                EmitSliceStartInfo(endMarker, markerCount, imgId, tid);
            }
        }

        profiles[tid]->BbFile.flush(); 
        profiles[tid]->first = false;            
    }
    
    
    VOID EmitSliceEndGlobal(ADDRINT endMarker, UINT32 imgId, 
            UINT32 markerCountOffset=0)
    {
        
        INT64 markerCount = markerCountOffset;
        
        if (globalProfile->first == true)
        {
            // Input merging will change the name of the input
            globalProfile->BbFile << "I: 0" << std::endl;
             // No "P:" record for global profile
            globalProfile->BbFile << "C: sum:dummy Command:" 
                << commandLine << std::endl;
            EmitSliceStartInfoGlobal(globalProfile->first_eip, 1,
                     globalProfile->first_eip_imgID);        
        }
        
        globalProfile->BbFile << "# Slice ending at " << std::dec 
            << globalProfile->CumulativeInstructionCountGlobal._count << std::endl;
        
        if ( !globalProfile->first || KnobEmitFirstSlice )
            globalProfile->BbFile << "T" ;

        for (BLOCK_MAP::const_iterator bi = block_map.begin(); 
            bi != block_map.end(); bi++)
        {
            BLOCK * block = bi->second;
            const BLOCK_KEY & key = bi->first;
            
            if (key.Contains(endMarker))
            {
                markerCount += block->CumulativeBlockCountGlobal();
            }
            
            if ( !globalProfile->first || KnobEmitFirstSlice )
                block->EmitSliceEndGlobal(globalProfile);
        }

        if ( !globalProfile->first || KnobEmitFirstSlice )
            globalProfile->BbFile << std::endl;

        if (_ldv_type != LDV_TYPE_NONE )
        {
            if ( !globalProfile->first || KnobEmitFirstSlice )
            {
                globalProfile->LdvFile << "T" ;
                globalProfile->EmitLDV();
                globalProfile->LdvFile << std::endl;
            }
        }

        if ( globalProfile->active  && !globalProfile->last)
        {
        // This is the start marker for the next slice (hence skipping for 'last') 
            if (KnobNoSymbolic)
            {
                globalProfile->BbFile << "M: " << std::hex << endMarker 
                    << " " << std::dec << markerCount << std::endl;
            }
            else
            {
                EmitSliceStartInfoGlobal(endMarker, markerCount, imgId);
            }
        }

        globalProfile->BbFile.flush(); 
        globalProfile->first = false;            
    }

    static ADDRINT GetFirstIP_If(THREADID tid, ISIMPOINT *isimpoint)
    {
        return !isimpoint->profiles[tid]->first_eip;
    }
    
    static VOID GetFirstIP_Then(VOID * ip, THREADID tid,
         ISIMPOINT *isimpoint, UINT32 imgID)
    {
        isimpoint->profiles[tid]->first_eip = reinterpret_cast<ADDRINT>(ip);
        isimpoint->profiles[tid]->first_eip_imgID = imgID;
    ;
        PIN_RemoveInstrumentation();        
    }
    

    static ADDRINT GetFirstIP_IfGlobal(ISIMPOINT *isimpoint)
    {
        return !isimpoint->globalProfile->first_eip;
    }
    
    static VOID GetFirstIP_ThenGlobal(VOID * ip,
         ISIMPOINT *isimpoint, UINT32 imgID)
    {
        isimpoint->globalProfile->first_eip = reinterpret_cast<ADDRINT>(ip);
        isimpoint->globalProfile->first_eip_imgID = imgID;
    ;
        PIN_RemoveInstrumentation();        
    }

    static ADDRINT CountBlock_If(BLOCK * block, THREADID tid, ISIMPOINT *isimpoint)
    {
        block->Execute(tid);
        
        isimpoint->profiles[tid]->SliceTimer -= block->StaticInstructionCount();
        isimpoint->profiles[tid]->last_block = block;
        
        return(isimpoint->profiles[tid]->SliceTimer < (INT64)0);
    }

    static ADDRINT CountBlock_IfGlobal(BLOCK * block, ISIMPOINT *isimpoint)
    {
        block->ExecuteGlobal();
        
       INT64 oldCount =  ATOMIC::OPS::Increment<INT64>
                (&isimpoint->globalProfile->SliceTimerGlobal._count, 
                -1*block->StaticInstructionCount()); 
        isimpoint->globalProfile->last_block = block;
        
        return ( (oldCount - block->StaticInstructionCount()) < (INT64)0);
    }

    static ADDRINT CountBlockAndTrackPrevious_If(BLOCK * block, THREADID tid, 
        ISIMPOINT *isimpoint)
    {
        block->Execute(tid, isimpoint->profiles[tid]->last_block, isimpoint);
        
        isimpoint->profiles[tid]->SliceTimer -= block->StaticInstructionCount();
        isimpoint->profiles[tid]->last_block = block;
        
        return(isimpoint->profiles[tid]->SliceTimer < 0);
    }    

    static ADDRINT CountBlockAndTrackPrevious_IfGlobal(BLOCK * block, 
        ISIMPOINT *isimpoint)
    {
        block->ExecuteGlobal(isimpoint->globalProfile->last_block, isimpoint);
        
        INT64 oldCount =  ATOMIC::OPS::Increment<INT64>
                (&isimpoint->globalProfile->SliceTimerGlobal._count, 
                -1*block->StaticInstructionCount()); 
        isimpoint->globalProfile->last_block = block;
        
        return ( (oldCount - block->StaticInstructionCount()) < (INT64)0);
    }    

    static VOID ResetSliceTimer(THREADID tid, ISIMPOINT *isimpoint)
    {
        if(isimpoint->profiles[tid]->length_queue.size())
        {
            isimpoint->profiles[tid]->CumulativeInstructionCount += 
            (isimpoint->profiles[tid]->CurrentSliceSize - 
                    isimpoint->profiles[tid]->SliceTimer);
            isimpoint->profiles[tid]->SliceTimer = 
                    (INT64) isimpoint->profiles[tid]->length_queue.front(); 
            isimpoint->profiles[tid]->CurrentSliceSize = 
                isimpoint->profiles[tid]->SliceTimer;
            isimpoint->profiles[tid]->length_queue.pop(); 
        }
        else
        {
            isimpoint->profiles[tid]->CumulativeInstructionCount += 
                    (isimpoint->profiles[tid]->CurrentSliceSize - 
                        isimpoint->profiles[tid]->SliceTimer);
            isimpoint->profiles[tid]->SliceTimer = 
                        isimpoint->KnobSliceSize;
            isimpoint->profiles[tid]->CurrentSliceSize = 
                    isimpoint->profiles[tid]->SliceTimer;
        }
    }

    static VOID ResetSliceTimerGlobal(ISIMPOINT *isimpoint)
    {
        if(isimpoint->globalProfile->length_queue.size())
        {
          ASSERT(0,"Global length queue NYT ");
        }
        else
        {
        ATOMIC::OPS::Increment<INT64>
                (&isimpoint->globalProfile->CumulativeInstructionCountGlobal._count, 
                    (isimpoint->globalProfile->CurrentSliceSizeGlobal._count - 
                        isimpoint->globalProfile->SliceTimerGlobal._count));
            isimpoint->globalProfile->SliceTimerGlobal._count = 
                        isimpoint->KnobSliceSize;
            isimpoint->globalProfile->CurrentSliceSizeGlobal._count = 
                    isimpoint->globalProfile->SliceTimerGlobal._count;
        }
    }

    static ADDRINT  CheckDelayedVectorEmission(THREADID tid, 
        ISIMPOINT *isimpoint)
    {
        return (isimpoint->VectorPending(tid));
    }

    static ADDRINT  CheckDelayedVectorEmissionGlobal( ISIMPOINT *isimpoint)
    {
        return (isimpoint->VectorPendingGlobal());
    }
    
    
    static VOID DelayedVectorEmission(BLOCK * block, THREADID tid, 
        ISIMPOINT *isimpoint)
    {
        // A slice ended but frequency vector
        // was not emitted. Do it now.
        isimpoint->EmitVectorForFriend(block->Key().Start(), 
            block->ImgId(), tid, isimpoint, /*markerOffset*/1);
        // This block is not yet executed and we are using its first
        // insAddr as a marker hence we provide an offset of 1.
    }

    
    static VOID DelayedVectorEmissionGlobal(BLOCK * block, ISIMPOINT *isimpoint)
    {
        // A slice ended but frequency vector
        // was not emitted. Do it now.
        isimpoint->EmitVectorForFriendGlobal(block->Key().Start(), 
            block->ImgId(), isimpoint, /*markerOffset*/1);
        // This block is not yet executed and we are using its first
        // insAddr as a marker hence we provide an offset of 1.
    }

    static VOID CountBlock_Then(BLOCK * block, THREADID tid, 
        ISIMPOINT *isimpoint)
    {
        if(!isimpoint->KnobEmitVectors) 
        {
           // do not output frequency vector but set a flag indicating
           // vector output is pending. The vector output will be 
           // triggered by another class.
           isimpoint->_vectorPending[tid] = TRUE;
        }
        else if(isimpoint->KnobDelayVectorEmission)
        {
           // do not output frequency vector but set a flag indicating
           // vector output is pending. The vector output will be 
           // done at the beginning of the next basic block.
           isimpoint->_vectorPending[tid] = TRUE;
        }
        else
        {
            isimpoint->ResetSliceTimer(tid, isimpoint);
            isimpoint->EmitSliceEnd(block->Key().End(), block->ImgId(), tid);
        }
    }

    static VOID CountBlock_ThenGlobal(BLOCK * block, ISIMPOINT *isimpoint)
    {
        if(!isimpoint->KnobEmitVectors) 
        {
           // do not output frequency vector but set a flag indicating
           // vector output is pending. The vector output will be 
           // triggered by another class.
           isimpoint->_vectorPendingGlobal = TRUE;
        }
        else if(isimpoint->KnobDelayVectorEmission)
        {
           // do not output frequency vector but set a flag indicating
           // vector output is pending. The vector output will be 
           // done at the beginning of the next basic block.
           isimpoint->_vectorPendingGlobal = TRUE;
        }
        else
        {
            isimpoint->ResetSliceTimerGlobal(isimpoint);
            isimpoint->EmitSliceEndGlobal(block->Key().End(), block->ImgId());
        }
    }

    VOID EmitVectorForFriendGlobal(ADDRINT marker, UINT32 imageid,
        ISIMPOINT *isimpoint, UINT32 markerCountOffset=0)
    {
        if(!isimpoint->_vectorPendingGlobal) return; // could be a race condition
        PIN_GetLock(&_globalProfileLock, 1);
        if (isimpoint->globalProfile->SliceTimerGlobal._count >=  0)
        {
          // some other thread did the outputting
          PIN_ReleaseLock(&_globalProfileLock);
          return;
        }
        isimpoint->_vectorPendingGlobal = FALSE;
        isimpoint->ResetSliceTimerGlobal(isimpoint);
        isimpoint->EmitSliceEndGlobal(marker, imageid, markerCountOffset);
        PIN_ReleaseLock(&_globalProfileLock);
    }

    VOID EmitVectorForFriend(ADDRINT marker, UINT32 imageid,
        THREADID tid, ISIMPOINT *isimpoint, UINT32 markerCountOffset=0)
    {
        ASSERTX(isimpoint->_vectorPending[tid]);
        isimpoint->_vectorPending[tid] = FALSE;
        isimpoint->ResetSliceTimer(tid, isimpoint);
        isimpoint->EmitSliceEnd(marker, imageid, tid, markerCountOffset);
    }

    // Lookup a block by its id.
    // Return block_map.end() if not found.
    BLOCK_MAP::const_iterator LookupBlock(INT32 id) const {
        BLOCK_MAP::const_iterator bi = block_map.begin();
        for (; bi != block_map.end(); bi++)
        {
            if (bi->second->Id() == id)
                return bi;
        }
        return bi;
    }

    // Lookup a block by its BBL key.
    // Create a new one and return it if it doesn't already exist.
    BLOCK * LookupBlock(BBL bbl)
    {
        BLOCK_KEY key(INS_Address(BBL_InsHead(bbl)), 
            INS_Address(BBL_InsTail(bbl)), BBL_Size(bbl));
        BLOCK_MAP::const_iterator bi = block_map.find(key);
        
        if (bi == block_map.end())
        {
            // Block not there, add it
            RTN rtn = INS_Rtn(BBL_InsHead(bbl));
            SEC sec = SEC_Invalid();
            IMG img = IMG_Invalid();
            if(RTN_Valid(rtn))
                sec = RTN_Sec(rtn);
            if(SEC_Valid(sec))
                img = SEC_Img(sec);

            BLOCK * block;
            if ( KnobEmitPrevBlockCounts )
            {
                block = new BLOCK(key, BBL_NumIns(bbl), 0,
                    IMG_Id(img));
            }
            else
            {
                block = new BLOCK(key, BBL_NumIns(bbl), _currentId[0],
                    IMG_Id(img));
                _currentId[0]++;
            }
            block_map.insert(BLOCK_PAIR(key, block));
            
            return block;
        }
        else
        {
            return bi->second;
        }
    }
    
    static VOID CountMemory(ADDRINT address, THREADID tid, ISIMPOINT *isimpoint)
    {
        isimpoint->profiles[tid]->ExecuteMemory(address);
    }
    
    static VOID CountMemoryGlobal(ADDRINT address, ISIMPOINT *isimpoint)
    {
        isimpoint->globalProfile->ExecuteMemoryGlobal(address);
    }

    BOOL DoInsertGetFirstIpInstrumentation()
    {
        UINT32 i;
        BOOL do_instrument = false;
        
        for ( i = 0; i < PIN_MAX_THREADS; i++ )
        {
            //cerr << " " << profiles[i]->active;
            if ( profiles[i]->active )
            {
                do_instrument |= !profiles[i]->first_eip;
                //cerr << ":" << !profiles[i]->first_eip;
            }
        }
        //cerr << " -> " << do_instrument << endl;    
        return do_instrument;
    }
    
    static VOID Trace(TRACE trace, VOID *v)
    {
        ISIMPOINT * isimpoint = reinterpret_cast<ISIMPOINT *>(v);
        enum CALL_ORDER thread_order = CALL_ORDER_DEFAULT;
        enum CALL_ORDER global_order = (CALL_ORDER)(CALL_ORDER_DEFAULT + 5);
        
        for (BBL bbl = TRACE_BblHead(trace); BBL_Valid(bbl);
            bbl = BBL_Next(bbl))
        {
            // find the block in the map or add it if new.
            BLOCK * block = isimpoint->LookupBlock(bbl);
            
            // insert insturmentation to get the first IP. Every thread
            // will call PIN_RemoveInstrumentation upon creation. This
            // ensures that the thread will insert instrumentation to log
            // the first eip. Once the first eip is logged,
            // PIN_RemoveInstrumentation is called again to remove the
            // instrumentation again.
            if ( isimpoint->KnobEmitFirstSlice &&
                isimpoint->DoInsertGetFirstIpInstrumentation() )
            {
                INS_InsertIfCall(BBL_InsHead(bbl), IPOINT_BEFORE,
                    (AFUNPTR)GetFirstIP_If, IARG_THREAD_ID, 
                    IARG_CALL_ORDER, thread_order,
                    IARG_PTR, isimpoint, IARG_END);
                INS_InsertThenCall(BBL_InsHead(bbl), IPOINT_BEFORE,
                     (AFUNPTR)GetFirstIP_Then, IARG_INST_PTR, IARG_THREAD_ID,
                     IARG_CALL_ORDER, thread_order,
                     IARG_PTR, isimpoint, IARG_UINT32, block->ImgId(),
                     IARG_END);
                if(KnobGlobal)
                {
                  INS_InsertIfCall(BBL_InsHead(bbl), IPOINT_BEFORE,
                    (AFUNPTR)GetFirstIP_IfGlobal, 
                    IARG_CALL_ORDER, global_order,
                    IARG_PTR, isimpoint, IARG_END);
                  INS_InsertThenCall(BBL_InsHead(bbl), IPOINT_BEFORE,
                     (AFUNPTR)GetFirstIP_ThenGlobal, IARG_INST_PTR, 
                     IARG_CALL_ORDER, global_order,
                     IARG_PTR, isimpoint, IARG_UINT32, block->ImgId(),
                     IARG_END);
                }
            }

            if ( isimpoint->KnobEmitPrevBlockCounts )
            {
                INS_InsertIfCall(BBL_InsTail(bbl), IPOINT_BEFORE,
                     (AFUNPTR)CountBlockAndTrackPrevious_If, IARG_PTR, block,
                    IARG_CALL_ORDER, thread_order,
                     IARG_THREAD_ID, IARG_PTR, isimpoint, IARG_END);
            }
            else
            {
                INS_InsertIfCall(BBL_InsTail(bbl), IPOINT_BEFORE,
                     (AFUNPTR)CountBlock_If, IARG_PTR, block, IARG_THREAD_ID,
                     IARG_CALL_ORDER, thread_order,
                     IARG_PTR, isimpoint, IARG_END);
            }
            INS_InsertThenCall(BBL_InsTail(bbl), IPOINT_BEFORE,
                     (AFUNPTR)CountBlock_Then, IARG_PTR, block,
                     IARG_CALL_ORDER, thread_order,
                     IARG_THREAD_ID, IARG_PTR, isimpoint, IARG_END);
            if(KnobGlobal)
            {
              if ( isimpoint->KnobEmitPrevBlockCounts )
              {
                INS_InsertIfCall(BBL_InsTail(bbl), IPOINT_BEFORE,
                     (AFUNPTR)CountBlockAndTrackPrevious_IfGlobal, IARG_PTR, block,
                    IARG_CALL_ORDER, global_order,
                     IARG_PTR, isimpoint, IARG_END);
              }
              else
              {
                INS_InsertIfCall(BBL_InsTail(bbl), IPOINT_BEFORE,
                     (AFUNPTR)CountBlock_IfGlobal, IARG_PTR, block, 
                     IARG_CALL_ORDER, global_order,
                     IARG_PTR, isimpoint, IARG_END);
              }
              INS_InsertThenCall(BBL_InsTail(bbl), IPOINT_BEFORE,
                     (AFUNPTR)CountBlock_ThenGlobal, IARG_PTR, block,
                     IARG_CALL_ORDER, global_order,
                     IARG_PTR, isimpoint, IARG_END);
            }

            if(isimpoint->KnobEmitVectors && 
                    isimpoint->KnobDelayVectorEmission) 
            {
                INS_InsertIfCall(BBL_InsHead(bbl), IPOINT_BEFORE,
                    (AFUNPTR)CheckDelayedVectorEmission, 
                    IARG_CALL_ORDER, (CALL_ORDER)thread_order + 1, // before CountBlock*()
                    IARG_THREAD_ID, IARG_PTR, isimpoint, IARG_END);
                INS_InsertThenCall(BBL_InsHead(bbl), IPOINT_BEFORE,
                    (AFUNPTR)DelayedVectorEmission, 
                    IARG_CALL_ORDER, (CALL_ORDER)thread_order + 1, // before CountBlock*()
                    IARG_PTR, block,IARG_THREAD_ID, 
                    IARG_PTR, isimpoint, IARG_END);
                if(KnobGlobal)
                {
                  INS_InsertIfCall(BBL_InsHead(bbl), IPOINT_BEFORE,
                    (AFUNPTR)CheckDelayedVectorEmissionGlobal, 
                    IARG_CALL_ORDER, (CALL_ORDER)global_order + 1, // before CountBlock*()
                    IARG_PTR, isimpoint, IARG_END);
                  INS_InsertThenCall(BBL_InsHead(bbl), IPOINT_BEFORE,
                    (AFUNPTR)DelayedVectorEmissionGlobal, 
                    IARG_CALL_ORDER, (CALL_ORDER)global_order + 1, // before CountBlock*()
                    IARG_PTR, block,
                    IARG_PTR, isimpoint, IARG_END);
                }
            }

            ISIMPOINT * isimpoint = reinterpret_cast<ISIMPOINT *>(v);
            if (isimpoint->_ldv_type != LDV_TYPE_NONE )
            {
                for(INS ins = BBL_InsHead(bbl); ; ins = INS_Next(ins))
                {
                   // We do not count AGEN instructions here in order to avoid instrumenting
                   // Emulated instruction in PIN.
                   // TBD - Support AGEN memory operands if needed
                   BOOL agen = false;
#if defined(EMX_INIT)
                   agen = EMU_ISA::IsAgen(ins);
#endif
                   if ((INS_IsMemoryRead(ins) || INS_IsMemoryWrite(ins)) && !agen)
                   {
                      for (UINT32 i = 0; i < INS_MemoryOperandCount(ins); i++)
                         INS_InsertCall(ins, IPOINT_BEFORE,
                            (AFUNPTR)CountMemory, IARG_MEMORYOP_EA, i,
                            IARG_THREAD_ID, IARG_PTR, isimpoint, IARG_END);
                      if(KnobGlobal)
                      {
                      for (UINT32 i = 0; i < INS_MemoryOperandCount(ins); i++)
                         INS_InsertCall(ins, IPOINT_BEFORE,
                            (AFUNPTR)CountMemoryGlobal, IARG_MEMORYOP_EA, i,
                            IARG_PTR, isimpoint, IARG_END);
                      }
                   }

                   if (ins == BBL_InsTail(bbl))
                      break;
                }
            }
        }
    }
    
    static VOID Image(IMG img, VOID * v)
    {
        ISIMPOINT * isimpoint = reinterpret_cast<ISIMPOINT *>(v);
        
        if(KnobGlobal)
        {
          isimpoint->globalProfile->OpenFileGlobal(isimpoint->Pid,
                     isimpoint->KnobOutputFile.Value(), 
                        isimpoint->_ldv_type != LDV_TYPE_NONE);
          isimpoint->globalProfile->BbFile << "G: " << IMG_Name(img)
                     << " LowAddress: " << std::hex  << IMG_LowAddress(img)
                     << " LoadOffset: " << std::hex << IMG_LoadOffset(img) << std::endl;
          isimpoint->globalProfile->BbFile.flush(); 
        }
        isimpoint->profiles[0]->OpenFile(0, isimpoint->Pid,
                     isimpoint->KnobOutputFile.Value(), 
                        isimpoint->_ldv_type != LDV_TYPE_NONE);
        isimpoint->img_manager.AddImage(img);
        isimpoint->profiles[0]->BbFile << "G: " << IMG_Name(img)
                     << " LowAddress: " << std::hex  << IMG_LowAddress(img)
                     << " LoadOffset: " << std::hex << IMG_LoadOffset(img) << std::endl;
    }


    static VOID ThreadStart(THREADID tid, CONTEXT *ctxt, INT32 flags, VOID *v)
    {
        ISIMPOINT * isimpoint = reinterpret_cast<ISIMPOINT *>(v);
        
        ASSERTX(tid < PIN_MAX_THREADS);
        isimpoint->profiles[tid]->OpenFile(tid, isimpoint->Pid,
                     isimpoint->KnobOutputFile.Value(),
                        isimpoint->_ldv_type != LDV_TYPE_NONE);
        isimpoint->profiles[tid]->active = true;
        PIN_RemoveInstrumentation();        
    }
    
    static VOID ProcessFini(INT32 code, VOID *v)
    {
        ISIMPOINT * isimpoint = reinterpret_cast<ISIMPOINT *>(v);
        
        if ( isimpoint->KnobEmitLastSlice &&
            isimpoint->globalProfile->SliceTimerGlobal._count != 
                isimpoint->globalProfile->CurrentSliceSizeGlobal._count )
        {
          BLOCK * block = isimpoint->globalProfile->last_block;
          if(isimpoint->KnobEmitVectors) 
          {
            isimpoint->globalProfile->last = true; // this is the last slice
            isimpoint->ResetSliceTimerGlobal(isimpoint);
            isimpoint->EmitSliceEndGlobal(block->Key().End(), block->ImgId());
          }
        }
        isimpoint->globalProfile->active = false;    
        isimpoint->EmitProgramEndGlobal(isimpoint);
        isimpoint->globalProfile->BbFile << "End of bb" << std::endl;
        isimpoint->globalProfile->BbFile.close();
    }

    static VOID ThreadFini(UINT32 tid, const CONTEXT *ctxt, INT32 code, VOID *v)
    {
        ISIMPOINT * isimpoint = reinterpret_cast<ISIMPOINT *>(v);
        
        if ( isimpoint->KnobEmitLastSlice &&
            isimpoint->profiles[tid]->SliceTimer != 
                isimpoint->profiles[tid]->CurrentSliceSize )
        {
          BLOCK * block = isimpoint->profiles[tid]->last_block;
          if(isimpoint->KnobEmitVectors) 
          {
            isimpoint->profiles[tid]->last = true; // this is the last slice
            isimpoint->ResetSliceTimer(tid, isimpoint);
            isimpoint->EmitSliceEnd(block->Key().End(), block->ImgId(), tid);
          }
        }
        isimpoint->profiles[tid]->active = false;    
        isimpoint->EmitProgramEnd(tid, isimpoint);
        isimpoint->profiles[tid]->BbFile << "End of bb" << std::endl;
        isimpoint->profiles[tid]->BbFile.close();
    }
    
    
    VOID GetCommand(int argc, char *argv[])
    {
        for (INT32 i = 0; i < argc; i++)
        {
            commandLine += " ";
            commandLine += argv[i];
        }
    }

    VOID activate(int argc, char** argv) 
    {
        if (isimpoint_knob)
        {
            if (KnobLDVType.Value() == "none")
                _ldv_type = LDV_TYPE_NONE;
            else if (KnobLDVType.Value() == "approx")
                _ldv_type = LDV_TYPE_APPROXIMATE;
            else if (KnobLDVType.Value() == "exact")
                _ldv_type = LDV_TYPE_EXACT;
            else
                ASSERT(0,"Invalid ldv_type: "+KnobLDVType.Value());
            AddInstrumentation(argc, argv);
        }
    }

    BOOL ParseFilenameTid(const std::string& str, std::string *fn, UINT32 *tidp)
    {
        size_t tidpos = str.find(":tid");
        if(tidpos == std::string::npos) return FALSE;
        std::string tidstr = str.substr(tidpos+4);
        *fn=str.substr(0, tidpos);
        *tidp = Uint32FromString(tidstr);
        return TRUE;
    }

    VOID AddInstrumentation(int argc, char *argv[])
    {
        GetCommand(argc, argv);
        
        //maxThread = MaxThreadsKnob.ValueInt64();
        profiles = new PROFILE* [PIN_MAX_THREADS];
        memset(profiles, 0, PIN_MAX_THREADS * sizeof(profiles[0]));
        if(KnobGlobal)
        {
          globalProfile = new PROFILE(KnobSliceSize, _ldv_type);
          globalProfile->active = true;
        }
        
        if (KnobPid)
        {
            Pid = getpid();
        }
        
        PIN_AddThreadStartFunction(ThreadStart, this);
        PIN_AddThreadFiniFunction(ThreadFini, this);
        if(KnobGlobal) PIN_AddFiniFunction(ProcessFini, this);
        
        for (THREADID tid = 0; tid < PIN_MAX_THREADS; tid++)
        {
            profiles[tid] = new PROFILE(KnobSliceSize, _ldv_type);
        }

        UINT32 num_length_files = KnobLengthFile.NumberOfValues();
        ASSERTX(num_length_files < PIN_MAX_THREADS);
        for (UINT32 i = 0; i < num_length_files; i++)
        {
                std::string val = KnobLengthFile.Value(i);
                std::string fn;
                UINT32 tid;
                BOOL tidfound  = ParseFilenameTid(val, &fn, &tid);
                if ( !tidfound)
                {
                    // skipping 'tidN' suffix ok only if one -lengthfile is
                    // provided and then tid 0 is assumed.
                    ASSERT(num_length_files==1, 
                        "missing 'tidN' suffix to lengthfile:"+val);
                    tid = 0;
                }
                profiles[tid]->ReadLengthFile((THREADID)tid, fn);
        }
        
#if defined(TARGET_MAC)
        // On Mac, ImageLoad() works only after we call PIN_InitSymbols().
        PIN_InitSymbols();
#endif
        
        TRACE_AddInstrumentFunction(Trace, this);
        IMG_AddInstrumentFunction(Image, this);    
    }
    
    VOID EmitProgramEnd(THREADID tid, const ISIMPOINT * isimpoint)
    {
        profiles[tid]->BbFile << "Dynamic instruction count "
             << std::dec << profiles[tid]->CumulativeInstructionCount << std::endl;
        profiles[tid]->BbFile << "SliceSize: " << std::dec << KnobSliceSize << std::endl;
        if ( KnobEmitPrevBlockCounts )
        {
            // Emit blocks in the order that they were first executed.
            for (UINT32 id = 1; id < _currentId[tid]; id++) {
                BLOCK_MAP::const_iterator bi = LookupBlock(id);
                if (bi != block_map.end())
                    bi->second->EmitProgramEnd(bi->first, tid, profiles[tid],
                        isimpoint);
            }
        }
        else
        {
            for (BLOCK_MAP::const_iterator bi = block_map.begin();
                 bi != block_map.end(); bi++)
            {
                bi->second->EmitProgramEnd(bi->first, tid, profiles[tid],
                     isimpoint);
            }
        }
    }

    VOID EmitProgramEndGlobal(const ISIMPOINT * isimpoint)
    {
        ASSERT(KnobGlobal, "-global_profile is disabled!");
        globalProfile->BbFile << "Dynamic instruction count "
             << std::dec << globalProfile->CumulativeInstructionCountGlobal._count 
                 << std::endl;
        globalProfile->BbFile << "SliceSize: " << std::dec << KnobSliceSize 
               << std::endl;
        if ( KnobEmitPrevBlockCounts )
        {
          ASSERT(0,"KnobEmitPrevBlockCounts in EmitProgramEndGlobal() NYT "); 
        }
        else
        {
            for (BLOCK_MAP::const_iterator bi = block_map.begin();
                 bi != block_map.end(); bi++)
            {
                bi->second->EmitProgramEndGlobal(bi->first, globalProfile,
                     isimpoint);
            }
        }
    }

    // read-only accessor.
    THREADID getCurrentId(THREADID tid) const {
        //ASSERTX(tid < PIN_MAX_THREADS);
        return _currentId[tid];
    }

    // increment _currentId and return incremented value.
    THREADID getNextCurrentId(THREADID tid) {
        ASSERTX(tid < PIN_MAX_THREADS);
        ASSERTX(KnobEmitPrevBlockCounts);
        return _currentId[tid]++;
    }

    // Check if this address starts a slice
    BOOL FoundInStartSlices(ADDRINT addr) const {
        PIN_GetLock((PIN_LOCK*)&_slicesLock, 1);
        BOOL found = _slices_start_set.find(addr) !=_slices_start_set.end();
        PIN_ReleaseLock((PIN_LOCK*)&_slicesLock);
        return found;
    }

    static KNOB_COMMENT knob_family;
    static KNOB<BOOL> isimpoint_knob;
    static KNOB<std::string> KnobOutputFile;
    static KNOB<INT64>  KnobSliceSize;
    static KNOB<BOOL>  KnobNoSymbolic;
    static KNOB<BOOL>  KnobEmitVectors;
    static KNOB<BOOL>  KnobDelayVectorEmission;
    static KNOB<BOOL>  KnobEmitFirstSlice;
    static KNOB<BOOL>  KnobEmitLastSlice;
    static KNOB<BOOL>  KnobEmitPrevBlockCounts;
    static KNOB<BOOL>  KnobPid;
    static KNOB<BOOL>  KnobGlobal;
    static KNOB<std::string> KnobLDVType;
    static KNOB<std::string> KnobLengthFile;
    LDV_TYPE _ldv_type;
};


#endif
