#include "trace_rtn.h"
#include "globals.h"
#include "threads.h"
#include "simulator.h"
#include "memory_tracker.h"
#include "recorder_control.h"
#include <iostream>

#define MALLOC "malloc"
#define CALLOC "calloc"
#define REALLOC "realloc"
#define FREE "free"
/*
   static void routineEnterMalloc(THREADID threadid, ADDRINT eip, ADDRINT esp, Sift::RoutineOpType opType)
   {
   if ((any_thread_in_detail || KnobRoutineTracingOutsideDetailed.Value()) && thread_data[threadid].output)
   {
   thread_data[threadid].output->RoutineChange(Sift::RoutineOpType::MallocEnter, eip, esp, thread_data[threadid].last_call_site);
   thread_data[threadid].last_routine = eip;
   thread_data[threadid].last_call_site = 0;
   }
   }
   */

bool isMain(ADDRINT ret)
{
	PIN_LockClient();
	IMG im = IMG_FindByAddress(ret);
	PIN_UnlockClient();
	int inMain = IMG_Valid(im) ? IMG_IsMainExecutable(im) : 0;
	return inMain;


}

static void routineEnter(THREADID threadid, ADDRINT eip, ADDRINT esp)
{
	if ((any_thread_in_detail || KnobRoutineTracingOutsideDetailed.Value()) && thread_data[threadid].output)
	{
		thread_data[threadid].output->RoutineChange(Sift::RoutineEnter, eip, esp, thread_data[threadid].last_call_site);
		thread_data[threadid].last_routine = eip;
		thread_data[threadid].last_call_site = 0;
	}
}


static void routineExit(THREADID threadid, ADDRINT eip, ADDRINT esp)
{
	if ((any_thread_in_detail || KnobRoutineTracingOutsideDetailed.Value()) && thread_data[threadid].output)
	{
		thread_data[threadid].output->RoutineChange(Sift::RoutineExit, eip, esp);
		thread_data[threadid].last_routine = -1;
	}
}


static void routineAssert(THREADID threadid, ADDRINT eip, ADDRINT esp)
{
	if ((any_thread_in_detail || KnobRoutineTracingOutsideDetailed.Value())
			&& thread_data[threadid].output && thread_data[threadid].last_routine != eip)
	{
		thread_data[threadid].output->RoutineChange(Sift::RoutineAssert, eip, esp);
		thread_data[threadid].last_routine = eip;
	}
}

static void routineCallSite(THREADID threadid, ADDRINT eip)
{
	thread_data[threadid].last_call_site = eip;
}

static void announceRoutine(INS ins)
{
	if (!thread_data[PIN_ThreadId()].output)
		return;

	ADDRINT eip = INS_Address(ins);
	RTN rtn = INS_Rtn(ins);
	IMG img = IMG_FindByAddress(eip);

	routines[eip] = true;

	INT32 column = 0, line = 0;
	std::string filename = "??";
	PIN_GetSourceLocation(eip, &column, &line, &filename);

	thread_data[PIN_ThreadId()].output->RoutineAnnounce(
			eip,
			RTN_Valid(rtn) ? RTN_Name(rtn).c_str() : "??",
			IMG_Valid(img) ? IMG_Name(img).c_str() : "??",
			IMG_Valid(img) ? IMG_LoadOffset(img) : 0,
			column, line, filename.c_str());
}

static void announceRoutine(RTN rtn)
{
	announceRoutine(RTN_InsHeadOnly(rtn));
}

static void announceInvalidRoutine()
{
	if (!thread_data[PIN_ThreadId()].output)
		return;

	routines[0] = true;
	thread_data[PIN_ThreadId()].output->RoutineAnnounce(0, "INVALID", "", 0, 0, 0, "");
}

static void routineCallback(RTN rtn, VOID *v)
{
	RTN_Open(rtn);

	if (routines.count(RTN_Address(rtn)) == 0)
		announceRoutine(rtn);


	RTN_InsertCall(rtn, IPOINT_BEFORE, AFUNPTR(routineEnter), IARG_THREAD_ID, IARG_ADDRINT, RTN_Address(rtn), IARG_REG_VALUE, REG_STACK_PTR, IARG_END);
	RTN_InsertCall(rtn, IPOINT_AFTER, AFUNPTR(routineExit), IARG_THREAD_ID, IARG_ADDRINT, RTN_Address(rtn), IARG_REG_VALUE, REG_STACK_PTR, IARG_END);

	RTN_Close(rtn);
}

static void traceCallback(TRACE trace, VOID *v)
{
	// At the start of each trace, check to see if this part of the code belongs to the function we think we're in.
	// This will detect longjmps and tail call elimination, and fix up the call stack appropriately.
	RTN rtn = TRACE_Rtn(trace);

	if (RTN_Valid(rtn))
	{
		if (routines.count(RTN_Address(rtn)) == 0)
		{
			RTN_Open(rtn);
			announceRoutine(rtn);
			RTN_Close(rtn);
		}

		TRACE_InsertCall(trace, IPOINT_BEFORE, AFUNPTR (routineAssert), IARG_THREAD_ID, IARG_ADDRINT, RTN_Address(rtn), IARG_REG_VALUE, REG_STACK_PTR, IARG_END);
	}
	else
	{
		if (routines.count(0) == 0)
			announceInvalidRoutine();

		TRACE_InsertCall(trace, IPOINT_BEFORE, AFUNPTR (routineAssert), IARG_THREAD_ID, IARG_ADDRINT, 0, IARG_REG_VALUE, REG_STACK_PTR, IARG_END);
	}

	// Call site identification
	for (BBL bbl = TRACE_BblHead(trace); BBL_Valid(bbl); bbl = BBL_Next(bbl))
		for(INS ins = BBL_InsHead(bbl); INS_Valid(ins); ins = INS_Next(ins))
		{
			if (INS_IsProcedureCall(ins))
			{
				announceRoutine(ins);
				INS_InsertCall(ins, IPOINT_BEFORE, AFUNPTR (routineCallSite), IARG_THREAD_ID, IARG_INST_PTR, IARG_END);
			}
		}
}

void ReallocBeforeImg(THREADID threadid, ADDRINT eip, ADDRINT calleip, ADDRINT initial_pointer, UINT64 size){

	//if(isMain(calleip)){
		
		if (thread_data[threadid].output){

			//std::cout << "[SIFT_REC] ReallocBefore EIP: " << eip << " CALLEIP: " << calleip << " Size: " << size << " Initial pointer: " << initial_pointer << std::endl;
			thread_data[threadid].last_routine_is_realloc = 1;
			thread_data[threadid].realloc_size = size;
			thread_data[threadid].realloc_init_pointer = initial_pointer;
		}
	//}
}



void ReallocAfterImg(THREADID threadid, ADDRINT eip,  ADDRINT pointer){
	if (!thread_data[threadid].output)
		openFile(threadid);

	if (thread_data[threadid].output)
	{
		if( thread_data[threadid].last_routine_is_realloc )
		{	
			
			//std::cout << "[SIFT_REC] ReallocAfter Pointer: " << pointer << std::endl;
			thread_data[threadid].output->ReallocEnter(thread_data[threadid].realloc_init_pointer,pointer,thread_data[threadid].realloc_size);
			thread_data[threadid].last_routine_is_realloc = 0;
			thread_data[threadid].realloc_size = 0;
			thread_data[threadid].realloc_init_pointer = 0;
		}
	}


}




void CallocBeforeImg(THREADID threadid, ADDRINT eip, ADDRINT calleip, UINT64 size, UINT64 type_size){

	//if(isMain(calleip)){
		
			//std::cout << "[SIFT_REC] CallocBefore EIP: " << eip << " CallEIP: " << calleip << " Size: " << size << std::endl;
			thread_data[threadid].last_routine_is_calloc = 1;
			thread_data[threadid].calloc_size = size*type_size;
		
	//}
}



void CallocAfterImg(THREADID threadid, ADDRINT eip,  ADDRINT pointer){
	if (!thread_data[threadid].output)
		openFile(threadid);
		
	

	if (thread_data[threadid].output)
	{
		if( thread_data[threadid].last_routine_is_calloc )
		{	
			
			//std::cout << "[SIFT_REC] CallocAfter Pointer: " << pointer << std::endl;
			thread_data[threadid].output->CallocEnter(pointer,thread_data[threadid].calloc_size);
			thread_data[threadid].last_routine_is_calloc = 0;
			thread_data[threadid].calloc_size = 0;
		}
	}


}


void FreeBeforeImg(THREADID threadid, ADDRINT eip, ADDRINT calleip, ADDRINT pointer){
	//if(isMain(calleip)){

		if (!thread_data[threadid].output)
		 	openFile(threadid);
		
		
		if (thread_data[threadid].output){

			//std::cout << "[SIFT_REC] FreeBefore EIP: " << eip << " CALLEIP: " << calleip << "Pointer: " << pointer << std::endl;
			thread_data[threadid].output->FreeEnter(pointer);
		}
	//}
}




void MallocBeforeImg(THREADID threadid, ADDRINT eip, ADDRINT calleip, UINT64 size){
	

	//if(isMain(calleip)){
		
			//std::cout << "[SIFT_REC] MallocBefore EIP: " << eip << " CALLEIP: " << calleip << " Size: " << size << std::endl;
			thread_data[threadid].last_routine_is_malloc = 1;
			thread_data[threadid].malloc_size = size;
		
	//}
}


void MallocAfterImg(THREADID threadid, ADDRINT eip,  ADDRINT pointer){

	if (!thread_data[threadid].output)
		 	openFile(threadid);
		
		

	if (thread_data[threadid].output)
	{
		if( thread_data[threadid].last_routine_is_malloc )
		{	
			//std::cout << "[SIFT_REC] MallocAfter Pointer: " << pointer << std::endl;
			
			thread_data[threadid].output->MallocEnter(pointer,thread_data[threadid].malloc_size);
			thread_data[threadid].last_routine_is_malloc = 0;
			thread_data[threadid].malloc_size = 0;
		}
	}


}

void RtnInsertCall(IMG img, char* funcname){

	RTN rtn = RTN_FindByName(img, funcname);
	if ( RTN_Valid(rtn) ){
		RTN_Open(rtn);
		if(!strcmp(funcname,MALLOC)){
			RTN_InsertCall( rtn,
					IPOINT_AFTER,
					(AFUNPTR) MallocAfterImg,
					IARG_THREAD_ID,
					IARG_ADDRINT, RTN_Address(rtn),
					IARG_FUNCRET_EXITPOINT_VALUE,	
					IARG_END);

			RTN_InsertCall( rtn,
					IPOINT_BEFORE,
					(AFUNPTR) MallocBeforeImg,
					IARG_THREAD_ID,
					IARG_ADDRINT, RTN_Address(rtn),	
					IARG_RETURN_IP,
					IARG_FUNCARG_ENTRYPOINT_VALUE,0,
					IARG_END);

		}
		if(!strcmp(funcname,CALLOC)){
			RTN_InsertCall( rtn,
					IPOINT_AFTER,
					(AFUNPTR) CallocAfterImg,
					IARG_THREAD_ID,
					IARG_ADDRINT, RTN_Address(rtn),
					IARG_FUNCRET_EXITPOINT_VALUE,	
					IARG_END);

			RTN_InsertCall( rtn,
					IPOINT_BEFORE,
					(AFUNPTR) CallocBeforeImg,
					IARG_THREAD_ID,
					IARG_ADDRINT, RTN_Address(rtn),	
					IARG_RETURN_IP,
					IARG_FUNCARG_ENTRYPOINT_VALUE,0,
					IARG_FUNCARG_ENTRYPOINT_VALUE,1,
					IARG_END);

		}
		if(!strcmp(funcname,REALLOC)){
			RTN_InsertCall( rtn,
					IPOINT_AFTER,
					(AFUNPTR) ReallocAfterImg,
					IARG_THREAD_ID,
					IARG_ADDRINT, RTN_Address(rtn),
					IARG_FUNCRET_EXITPOINT_VALUE,	
					IARG_END);

			RTN_InsertCall( rtn,
					IPOINT_BEFORE,
					(AFUNPTR) ReallocBeforeImg,
					IARG_THREAD_ID,
					IARG_ADDRINT, RTN_Address(rtn),	
					IARG_RETURN_IP,
					IARG_FUNCARG_ENTRYPOINT_VALUE,0,
					IARG_FUNCARG_ENTRYPOINT_VALUE,1,
					IARG_END);

		}
		
		if(!strcmp(funcname,FREE)){

			RTN_InsertCall( rtn,
					IPOINT_BEFORE,
					(AFUNPTR) FreeBeforeImg,
					IARG_THREAD_ID,
					IARG_ADDRINT, RTN_Address(rtn),	
					IARG_RETURN_IP,
					IARG_FUNCARG_ENTRYPOINT_VALUE,0,
					IARG_END);

		}
		
		
		
		
		RTN_Close(rtn);
	}

}

void Image( IMG img, void*)
{

	RtnInsertCall(img, (CHAR*) MALLOC);
	RtnInsertCall(img, (CHAR*) CALLOC);
	RtnInsertCall(img, (CHAR*) REALLOC);
	RtnInsertCall(img, (CHAR*) FREE);
}

void initRoutineTracer()
{
	RTN_AddInstrumentFunction(routineCallback, 0);
	TRACE_AddInstrumentFunction(traceCallback, 0);
	IMG_AddInstrumentFunction(Image, 0);
}
