#ifndef __SIFT_WRITER_H
#define __SIFT_WRITER_H

#include "sift.h"
#include "sift_format.h"

#include <unordered_map>
#include <fstream>
#include <assert.h>

class vistream;
class vostream;

namespace Sift
{
   class Writer
   {
      typedef void (*GetCodeFunc)(uint8_t *dst, const uint8_t *src, uint32_t size);
      typedef void (*GetCodeFunc2)(uint8_t *dst, const uint8_t *src, uint32_t size, void *data);
      typedef bool (*HandleAccessMemoryFunc)(void *arg, MemoryLockType lock_signal, MemoryOpType mem_op, uint64_t d_addr, uint8_t *data_buffer, uint32_t data_size);

      private:
         vostream *output;
         vistream *response;
         GetCodeFunc getCodeFunc;
         GetCodeFunc2 getCodeFunc2;
         void *getCodeFunc2Data;
         HandleAccessMemoryFunc handleAccessMemoryFunc;
         void *handleAccessMemoryArg;
         uint64_t ninstrs, hsize[16], haddr[MAX_DYNAMIC_ADDRESSES+1], nbranch, npredicate, ninstrsmall, ninstrext;

         uint64_t last_address;
         std::unordered_map<uint64_t, bool> icache;
         int fd_va;
         std::unordered_map<intptr_t, bool> m_va2pa;
         char *m_response_filename;
         uint32_t m_id;
         bool m_requires_icache_per_insn;
         bool m_send_va2pa_mapping;

         void initResponse();
         void handleMemoryRequest(Record &respRec);
         void send_va2pa(uint64_t va);
         uint64_t va2pa_lookup(uint64_t va);

	 void frontEndStop();

      public:
         Writer(const char *filename, GetCodeFunc getCodeFunc, bool useCompression = false, const char *response_filename = "", uint32_t id = 0, bool arch32 = false, bool requires_icache_per_insn = false, bool send_va2pa_mapping = false, GetCodeFunc2 getCodeFunc2 = NULL, void *GetCodeFunc2Data = NULL);
         ~Writer();
         void End();
         void Instruction(uint64_t addr, uint8_t size, uint8_t num_addresses, uint64_t addresses[], bool is_branch, bool taken, bool is_predicate, bool executed);
         Mode InstructionCount(uint32_t icount);
         void CacheOnly(uint8_t icount, CacheOnlyType type, uint64_t eip, uint64_t address);
         void Output(uint8_t fd, const char *data, uint32_t size);
         uint64_t Syscall(uint16_t syscall_number, const char *data, uint32_t size);
         int32_t NewThread(bool record_threads);
         int32_t Join(int32_t);
         Mode Sync();
         uint64_t Magic(uint64_t a, uint64_t b, uint64_t c);
         bool Emulate(Sift::EmuType type, Sift::EmuRequest &req, Sift::EmuReply &res);
         int32_t Fork();
         void RoutineChange(Sift::RoutineOpType event, uint64_t eip, uint64_t esp, uint64_t callEip = 0);
         void RoutineAnnounce(uint64_t eip, const char *name, const char *imgname, uint64_t offset, uint32_t line, uint32_t column, const char *filename);
         void ISAChange(uint32_t new_isa);
         bool IsOpen();

         void setHandleAccessMemoryFunc(HandleAccessMemoryFunc func, void* arg = NULL) { assert(func); handleAccessMemoryFunc = func; handleAccessMemoryArg = arg; }
   };
};

#endif // __SIFT_WRITER_H
