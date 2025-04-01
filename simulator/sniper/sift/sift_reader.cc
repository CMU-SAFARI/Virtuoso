#include "sift_reader.h"
#include "sift_format.h"
#include "sift_utils.h"
#include "zfstream.h"

#include <iostream>
#include <fstream>
#include <cassert>
#include <cstring>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

// Enable (>0) to print out everything we read
#define VERBOSE 0
#define VERBOSE_HEX 0
#define VERBOSE_ICACHE 0

Sift::Reader::Reader(const char *filename, const char *response_filename, uint32_t id)
   : input(NULL)
   , response(NULL)
   , handleInstructionCountFunc(NULL)
   , handleInstructionCountArg(NULL)
   , handleCacheOnlyFunc(NULL)
   , handleCacheOnlyArg(NULL)
   , handleOutputFunc(NULL)
   , handleOutputArg(NULL)
   , handleSyscallFunc(NULL)
   , handleSyscallArg(NULL)
   , handleNewThreadFunc(NULL)
   , handleNewThreadArg(NULL)
   , handleForkFunc(NULL)
   , handleForkArg(NULL)
   , handleJoinFunc(NULL)
   , handleJoinArg(NULL)
   , handleMagicFunc(NULL)
   , handleMagicArg(NULL)
   , handleEmuFunc(NULL)
   , handleEmuArg(NULL)
   , handleRoutineChangeFunc(NULL)
   , handleRoutineAnnounceFunc(NULL)
   , handleRoutineArg(NULL)   
   , filesize(0)
   , last_address(0)
   , icache()
   , m_id(id)
   , m_trace_has_pa(false)
   , m_seen_end(false)
   , m_last_sinst(NULL)
   , m_isa(0)
{
   m_filename = strdup(filename);
   m_response_filename = strdup(response_filename);

   // initing stream here could cause deadlock when using pipes, as this should be able to be new()ed from
   // a thread that should not block
}

Sift::Reader::~Reader()
{
   free(m_filename);
   free(m_response_filename);
   if (input)
      delete input;
   if (response)
      delete response;
   for(std::unordered_map<uint64_t, const uint8_t*>::iterator i = icache.begin() ; i != icache.end() ; ++i)
   {
      delete [] (*i).second;
   }
   for(std::unordered_map<uint64_t, const StaticInstruction*>::iterator i = scache.begin() ; i != scache.end() ; ++i)
   {
      delete (*i).second;
   }
}

bool Sift::Reader::initStream()
{
   #if VERBOSE > 0
   std::cerr << "[DEBUG:" << m_id << "] InitStream Attempting Open" << std::endl;
   #endif

   inputstream = new std::ifstream(m_filename, std::ios::in);

   if ((!inputstream->is_open()) || (!inputstream->good()))
   {
      std::cerr << "[SIFT:" << m_id << "] Cannot open " << m_filename << "\n";
      return false;
   }

   struct stat filestatus;
   stat(m_filename, &filestatus);
   filesize = filestatus.st_size;

   input = new vifstream(inputstream);

   Sift::Header hdr;
   input->read(reinterpret_cast<char*>(&hdr), sizeof(hdr));
   if (hdr.magic != Sift::MagicNumber)
   {
      std::cerr << "[SIFT:" << m_id << "] Invalid magic number\n";
      return false;
   }
   if (hdr.size != 0)
   {
      std::cerr << "[SIFT:" << m_id << "] Invalid header size\n";
   }

#if SIFT_USE_ZLIB
   if (hdr.options & CompressionZlib)
   {
      input = new izstream(input);
      hdr.options &= ~CompressionZlib;
   }
#else
   if (hdr.options & CompressionZlib)
   {
      std::cerr << "[SIFT:" << m_id << "] Error: Compression requested, but disabled at compile time.\n";
   }
#endif

   if (hdr.options & ArchIA32)
   {
      hdr.options &= ~ArchIA32;
   }

   if (hdr.options & PhysicalAddress)
   {
      m_trace_has_pa = true;
      hdr.options &= ~PhysicalAddress;
   }

   hdr.options &= ~IcacheVariable;

   // Make sure there are no unrecognized options
   if (hdr.options != 0)
   {
      return false;
   }

   #if VERBOSE > 0
   std::cerr << "[DEBUG:" << m_id << "] InitStream Connection Open" << std::endl;
   #endif

   return true;
}

bool Sift::Reader::initResponse()
{
   if (!response)
   {
      if (strcmp(m_response_filename, "") == 0)
      {
         std::cerr << "[SIFT:" << m_id << "] Response filename not set\n";
         return false;
      }
      response = new vofstream(m_response_filename, std::ios::out);
   }

   if ((!response->is_open()) || (response->fail()))
   {
      std::cerr << "[SIFT:" << m_id << "] Cannot open " << m_response_filename << "\n";
      return false;
   }

   return true;
}

bool Sift::Reader::Read(Instruction &inst)
{
   if (input == NULL)
   {
      if (!initStream())
      {
         std::cerr << "[SIFT:" << m_id << "] Error: initStream failed\n";
         return false;
      }
   }

   while(!m_seen_end)
   {
      Record rec;
      uint8_t byte = input->peek();
      if (input->fail())
      {
         std::cerr << "[SIFT:" << m_id << "] Error: " << strerror(errno) << "\n";
         return false;
      }

      if (byte == 0)
      {
         // Other
         input->read(reinterpret_cast<char*>(&rec), sizeof(rec.Other));
         switch(rec.Other.type)
         {
            case RecOtherEnd:
               assert(rec.Other.size == 0);
               m_seen_end = true;
               // disable EndResponse as it causes lockups with sift_recorder
               //sendSimpleResponse(RecOtherEndResponse);
               return false;
            case RecOtherIcache:
            {
               assert(rec.Other.size == sizeof(uint64_t) + ICACHE_SIZE);
               uint64_t address;
               uint8_t *bytes = new uint8_t[ICACHE_SIZE];
               input->read(reinterpret_cast<char*>(&address), sizeof(uint64_t));
               input->read(reinterpret_cast<char*>(bytes), ICACHE_SIZE);
               icache[address] = bytes;
               break;
            }
            case RecOtherIcacheVariable:
            {
               #if VERBOSE_ICACHE
               std::cerr << __FUNCTION__ << ": rec=" << std::endl;
               hexdump(&rec, sizeof(rec.Other));
               #endif
               uint64_t address;
               size_t size = rec.Other.size - sizeof(uint64_t);
               input->read(reinterpret_cast<char*>(&address), sizeof(uint64_t));
               size_t size_left = size;
               while (size_left > 0)
               {
                  uint64_t base_addr = address & ICACHE_PAGE_MASK;
                  if (icache.count(base_addr) == 0)
                     icache[base_addr] = new uint8_t[ICACHE_SIZE];
                  uint64_t offset = address & ICACHE_OFFSET_MASK;
                  size_t read_amount = std::min(size_left, size_t(ICACHE_SIZE - offset));
                  input->read(const_cast<char*>(reinterpret_cast<const char*>(&(icache[base_addr][offset]))), read_amount);

                  #if VERBOSE_ICACHE
                  std::cerr << __FUNCTION__ << ": Wrote " << read_amount << " bytes to 0x" << std::hex << (void*)&(icache[base_addr][offset]) << std::dec << std::endl;
                  hexdump(&(icache[base_addr][offset]), read_amount);
                  #endif

                  size_left -= read_amount;
                  address = base_addr + ICACHE_SIZE;
               }
               break;
            }
            case RecOtherLogical2Physical:
            {
               assert(rec.Other.size == 2 * sizeof(uint64_t));
               uint64_t vp, pp;
               input->read(reinterpret_cast<char*>(&vp), sizeof(uint64_t));
               input->read(reinterpret_cast<char*>(&pp), sizeof(uint64_t));
               vcache[vp] = pp;
               break;
            }
            case RecOtherInstructionCount:
            {
               #if VERBOSE > 0
               std::cerr << "[DEBUG:" << m_id << "] Read InstructionCount" << std::endl;
               #endif
               assert(rec.Other.size == sizeof(uint32_t));
               uint32_t icount;
               input->read(reinterpret_cast<char*>(&icount), sizeof(icount));
               Mode mode = ModeUnknown;
               if (handleInstructionCountFunc)
                  mode = handleInstructionCountFunc(handleInstructionCountArg, icount);
               sendSimpleResponse(RecOtherSyncResponse, &mode, sizeof(Mode));
               break;
            }
            case RecOtherCacheOnly:
            {
               #if VERBOSE > 0
               std::cerr << "[DEBUG:" << m_id << "] Read CacheOnly" << std::endl;
               #endif
               assert(rec.Other.size == sizeof(uint8_t) + sizeof(uint8_t) + sizeof(uint64_t) + sizeof(uint64_t));
               uint8_t icount, type;
               uint64_t eip, address;
               input->read(reinterpret_cast<char*>(&icount), sizeof(uint8_t));
               input->read(reinterpret_cast<char*>(&type), sizeof(uint8_t));
               input->read(reinterpret_cast<char*>(&eip), sizeof(uint64_t));
               input->read(reinterpret_cast<char*>(&address), sizeof(uint64_t));
               if (handleCacheOnlyFunc)
                  handleCacheOnlyFunc(handleCacheOnlyArg, icount, (Sift::CacheOnlyType)type, eip, address);
               break;
            }
            case RecOtherOutput:
            {
               #if VERBOSE > 0
               std::cerr << "[DEBUG:" << m_id << "] Read Output" << std::endl;
               #endif
               assert(rec.Other.size > sizeof(uint8_t));
               uint8_t fd;
               uint32_t size = rec.Other.size - sizeof(uint8_t);
               uint8_t *bytes = new uint8_t[size];
               input->read(reinterpret_cast<char*>(&fd), sizeof(uint8_t));
               input->read(reinterpret_cast<char*>(bytes), size);
               if (handleOutputFunc)
                  handleOutputFunc(handleOutputArg, fd, bytes, size);
               delete [] bytes;
               break;
            }
            case RecOtherSyscallRequest:
            {
               #if VERBOSE > 0
               std::cerr << "[DEBUG:" << m_id << "] Read SyscallRequest" << std::endl;
               #endif
               assert(rec.Other.size > sizeof(uint16_t));
               uint16_t syscall_number;
               uint32_t size = rec.Other.size - sizeof(uint16_t);
               uint8_t *bytes = new uint8_t[size];
               input->read(reinterpret_cast<char*>(&syscall_number), sizeof(uint16_t));
               input->read(reinterpret_cast<char*>(bytes), size);
               #if VERBOSE_HEX > 0
               hexdump((char*)&rec, sizeof(rec.Other));
               hexdump((char*)&syscall_number, sizeof(syscall_number));
               hexdump((char*)bytes, size);
               #endif
               #if VERBOSE > 1
               for (unsigned i = 0 ; i < (size/8) ; i++)
               {
                  std::cerr << __FUNCTION__ << ": syscall args[" << i << "] = " << ((uint64_t*)bytes)[i] << std::endl;
               }
               #endif

               assert(handleSyscallFunc);
               if (handleSyscallFunc)
               {
                  #if VERBOSE > 0
                  std::cerr << "[DEBUG:" << m_id << "] HandleSyscall" << std::endl;
                  #endif
                  uint64_t ret = handleSyscallFunc(handleSyscallArg, syscall_number, bytes, size);
                  sendSyscallResponse(ret);
               }
               delete [] bytes;
               break;
            }
            case RecOtherNewThread:
            {
               assert(rec.Other.size == 0);
               assert(handleNewThreadFunc);
               if (handleNewThreadFunc)
               {
                  #if VERBOSE > 0
                  std::cerr << "[DEBUG:" << m_id << "] HandleNewThread" << std::endl;
                  #endif
                  int32_t ret = handleNewThreadFunc(handleNewThreadArg);
                  sendSimpleResponse(RecOtherNewThreadResponse, &ret, sizeof(ret));
                  #if VERBOSE > 0
                  std::cerr << "[DEBUG:" << m_id << "] HandleNewThread Done" << std::endl;
                  #endif
               }
               break;
            }
            case RecOtherJoin:
            {
               int32_t thread;
               assert(rec.Other.size == sizeof(thread));
               input->read(reinterpret_cast<char*>(&thread), sizeof(thread));
               assert(handleJoinFunc);
               if (handleJoinFunc)
               {
                  #if VERBOSE > 0
                  std::cerr << "[DEBUG:" << m_id << "] HandleJoin" << std::endl;
                  #endif
                  int32_t ret = handleJoinFunc(handleJoinArg, thread);
                  sendSimpleResponse(RecOtherJoinResponse, &ret, sizeof(ret));
                  #if VERBOSE > 0
                  std::cerr << "[DEBUG:" << m_id << "] HandleJoin Done" << std::endl;
                  #endif
               }
               break;
            }
            case RecOtherSync:
            {
               assert(rec.Other.size == 0);
               Mode mode = ModeUnknown;
               if (handleInstructionCountFunc)
                  mode = handleInstructionCountFunc(handleInstructionCountArg, 0);
               sendSimpleResponse(RecOtherSyncResponse, &mode, sizeof(Mode));
               break;
            }
            case RecOtherFork:
            {
               assert(rec.Other.size == 0);
               assert(handleForkFunc);
               if(handleForkFunc)
               {
                  #if VERBOSE > 0
                  std::cerr << "[DEBUG:" << m_id << "] HandleFork" << std::endl;
                  #endif
                  int32_t ret = handleForkFunc(handleForkArg);
                  sendSimpleResponse(RecOtherForkResponse, &ret, sizeof(ret));
                  #if VERBOSE > 0
                  std::cerr << "[DEBUG:" << m_id << "] HandleFork Done" << std::endl;
                  #endif
               }
               break;
            }
            case RecOtherMagicInstruction:
            {
               assert(rec.Other.size == 3 * sizeof(uint64_t));
               uint64_t a, b, c;
               input->read(reinterpret_cast<char*>(&a), sizeof(uint64_t));
               input->read(reinterpret_cast<char*>(&b), sizeof(uint64_t));
               input->read(reinterpret_cast<char*>(&c), sizeof(uint64_t));
               uint64_t result;
               if (handleMagicFunc)
               {
                  result = handleMagicFunc(handleMagicArg, a, b, c);
               }
               else
               {
                  result = a; // Do not modify GAX register
               }
               sendSimpleResponse(RecOtherMagicInstructionResponse, &result, sizeof(result));
               break;
            }
            case RecOtherEmu:
            {
               assert(rec.Other.size <= sizeof(uint16_t) + sizeof(EmuRequest));
               uint16_t type; EmuRequest req;
               input->read(reinterpret_cast<char*>(&type), sizeof(uint16_t));
               input->read(reinterpret_cast<char*>(&req), rec.Other.size - sizeof(uint16_t));
               bool result = false; EmuReply res = {};
               if (handleEmuFunc)
               {
                  result = handleEmuFunc(handleEmuArg, EmuType(type), req, res);
               }
               sendEmuResponse(result, res);
               break;
            }
            case RecOtherRoutineChange:
            {
               assert(rec.Other.size == sizeof(uint8_t) + 3 * sizeof(uint64_t));
               uint8_t event;
               uint64_t eip, esp, callEip;
               input->read(reinterpret_cast<char*>(&event), sizeof(uint8_t));
               input->read(reinterpret_cast<char*>(&eip), sizeof(uint64_t));
               input->read(reinterpret_cast<char*>(&esp), sizeof(uint64_t));
               input->read(reinterpret_cast<char*>(&callEip), sizeof(uint64_t));
               if (handleRoutineChangeFunc)
                  handleRoutineChangeFunc(handleRoutineArg, Sift::RoutineOpType(event), eip, esp, callEip);
               break;
            }
            case RecOtherRoutineAnnounce:
            {
               uint64_t eip, offset;
               uint16_t len_name, len_imgname, len_filename;
               char *name, *imgname, *filename;
               uint32_t line, column;
               input->read(reinterpret_cast<char*>(&eip), sizeof(uint64_t));
               input->read(reinterpret_cast<char*>(&len_name), sizeof(uint16_t));
               name = (char*)malloc(len_name);
               input->read(name, len_name);
               input->read(reinterpret_cast<char*>(&len_imgname), sizeof(uint16_t));
               imgname = (char*)malloc(len_imgname);
               input->read(imgname, len_imgname);
               input->read(reinterpret_cast<char*>(&offset), sizeof(uint64_t));
               input->read(reinterpret_cast<char*>(&line), sizeof(uint32_t));
               input->read(reinterpret_cast<char*>(&column), sizeof(uint32_t));
               input->read(reinterpret_cast<char*>(&len_filename), sizeof(uint16_t));
               filename = (char*)malloc(len_filename);
               input->read(filename, len_filename);
               if (handleRoutineAnnounceFunc)
                  handleRoutineAnnounceFunc(handleRoutineArg, eip, name, imgname, offset, line, column, filename);
               free(name);
               free(filename);
               break;
            }            
            case RecOtherISAChange:
            { 
               assert(rec.Other.size == sizeof(uint32_t));
               uint32_t new_isa;
               input->read(reinterpret_cast<char*>(&new_isa), sizeof(new_isa));
               m_isa = new_isa; // save here new ISA mode value

               break;
            }
            default:
            {
               uint8_t *bytes = new uint8_t[rec.Other.size];
               input->read(reinterpret_cast<char*>(bytes), rec.Other.size);
               delete [] bytes;
               break;
            }
         }
         continue;
      }

      uint8_t size;
      uint64_t addr;

      if ((byte & 0xf) != 0)
      {
         // Instruction
         input->read(reinterpret_cast<char*>(&rec), sizeof(rec.Instruction));

         #if VERBOSE_HEX > 2
         hexdump(&rec, sizeof(rec.Instruction));
         #endif

         size = rec.Instruction.size;
         addr = last_address;
         inst.num_addresses = rec.Instruction.num_addresses;
         inst.is_branch = rec.Instruction.is_branch;
         inst.taken = rec.Instruction.taken;
         inst.is_predicate = false;
         inst.executed = true;
         inst.isa = m_isa;
      }
      else
      {
         // InstructionExt
         input->read(reinterpret_cast<char*>(&rec), sizeof(rec.InstructionExt));

         #if VERBOSE_HEX > 2
         hexdump(&rec, sizeof(rec.InstructionExt));
         #endif

         size = rec.InstructionExt.size;
         addr = rec.InstructionExt.addr;
         inst.num_addresses = rec.InstructionExt.num_addresses;
         inst.is_branch = rec.InstructionExt.is_branch;
         inst.taken = rec.InstructionExt.taken;
         inst.is_predicate = rec.InstructionExt.is_predicate;
         inst.executed = rec.InstructionExt.executed;
         inst.isa = m_isa;

         last_address = addr;
      }

      last_address += size;

      for(int i = 0; i < inst.num_addresses; ++i)
         input->read(reinterpret_cast<char*>(&inst.addresses[i]), sizeof(uint64_t));

      inst.sinst = getStaticInstruction(addr, size);

      #if VERBOSE_HEX > 2
      hexdump(inst.sinst->data, inst.sinst->size);
      #endif
      #if VERBOSE > 2
      printf("%016lx (%d) A%u %c%c %c%c\n", inst.sinst->addr, inst.sinst->size, inst.num_addresses, inst.is_branch?'B':'.', inst.is_branch?(inst.taken?'T':'.'):'.', inst.is_predicate?'C':'.', inst.is_predicate?(inst.executed?'E':'n'):'.');
      #endif

      return true;
   }

   // We should not return false (no more instructions) unless we get the End packet.
   // Return true in case we get to this point (which we shouldn't).
   return true;
}

bool Sift::Reader::AccessMemory(MemoryLockType lock_signal, MemoryOpType mem_op, uint64_t d_addr, uint8_t *data_buffer, uint32_t data_size)
{
   #if VERBOSE > 0
   if (mem_op == MemWrite)
      std::cerr << "[DEBUG:" << m_id << "] Write MemoryRequest - Write" << std::endl;
   if (mem_op == MemWrite)
      std::cerr << "[DEBUG:" << m_id << "] Write MemoryRequest - Read" << std::endl;
   #endif

   if (input == NULL)
   {
      if (!initStream())
      {
         std::cerr << "[SIFT:" << m_id << "] Error: initStream failed\n";
         return false;
      }
   }

   if (!initResponse())
   {
      std::cerr << "[SIFT:" << m_id << "] Error: initResponse failed\n";
      return false;
   }

   Record rec;
   rec.Other.zero = 0;
   rec.Other.type = RecOtherMemoryRequest;
   rec.Other.size = sizeof(d_addr) + sizeof(data_size) + sizeof(lock_signal) + sizeof(mem_op);
   if (mem_op == MemWrite)
   {
      rec.Other.size += data_size;
   }
   response->write(reinterpret_cast<char*>(&rec), sizeof(rec.Other));
   response->write(reinterpret_cast<char*>(&d_addr), sizeof(d_addr));
   response->write(reinterpret_cast<char*>(&data_size), sizeof(data_size));
   response->write(reinterpret_cast<char*>(&lock_signal), sizeof(lock_signal));
   response->write(reinterpret_cast<char*>(&mem_op), sizeof(mem_op));
   if (mem_op == MemWrite)
   {
      response->write(reinterpret_cast<char*>(data_buffer), data_size);
   }
   response->flush();

   #if VERBOSE > 0
   std::cerr << "[DEBUG:" << m_id << "] Read MemoryResponse" << std::endl;
   #endif
   uint64_t addr;
   MemoryOpType type;
   input->read(reinterpret_cast<char*>(&rec), sizeof(rec.Other));
   #if VERBOSE_HEX > 0
   hexdump((char*)&rec, sizeof(rec.Other));
   #endif
   if (rec.Other.type != RecOtherMemoryResponse)
   {
      std::cerr << "[SIFT:" << m_id << "] Error: Invalid response. Expected RecOtherMemoryResponse\n";
      return false;
   }
   input->read(reinterpret_cast<char*>(&addr), sizeof(addr));
   input->read(reinterpret_cast<char*>(&type), sizeof(type));
   #if VERBOSE_HEX > 0
   hexdump((char*)&addr, sizeof(addr));
   hexdump((char*)&type, sizeof(type));
   #endif
   if (addr != d_addr)
   {
      std::cerr << "[SIFT:" << m_id << "] Error: Invalid response. Expected addresses to match\n";
      return false;
   }

   uint32_t payload_size = rec.Other.size - sizeof(addr) - sizeof(type);

   if (mem_op == MemRead)
   {
      #if VERBOSE > 0
      std::cerr << "[DEBUG:" << m_id << "] Read MemoryResponse - Read Return Data" << std::endl;
      #endif
      if (data_size != payload_size)
      {
	 std::cerr << "[SIFT:" << m_id << "] Error: Invalid response. Expected payload size to match\n";
	 return false;
      }
      input->read(reinterpret_cast<char*>(data_buffer), data_size);
      #if VERBOSE_HEX > 0
      hexdump((char*)data_buffer, data_size);
      #endif
   }
   else if (mem_op == MemWrite)
   {
      if (payload_size != 0)
      {
	 std::cerr << "[SIFT:" << m_id << "] Error: Invalid response. Expected payload size of 0 for MemWrite\n";
	 return false;
      }
   }
   else
   {
      std::cerr << "Sift::Reader::" << __FUNCTION__ << ": invalid return memory op type" << std::endl;
      return false;
   }

   return true;
}

const Sift::StaticInstruction* Sift::Reader::staticInfoInstruction(uint64_t addr, uint8_t size)
{
   StaticInstruction *sinst = new StaticInstruction();
   sinst->addr = addr;
   sinst->size = size;
   sinst->next = NULL;

   uint8_t * dst = sinst->data;
   uint64_t base_addr = addr & ICACHE_PAGE_MASK;
   while(size > 0)
   {
      uint32_t offset = (dst == sinst->data) ? addr & ICACHE_OFFSET_MASK : 0;
      uint32_t _size = std::min(uint32_t(size), ICACHE_SIZE - offset);
      assert(icache.count(base_addr));
      memcpy(dst, icache[base_addr] + offset, _size);
      dst += _size;
      size -= _size;
      base_addr += ICACHE_SIZE;
   }

   return sinst;
}

const Sift::StaticInstruction* Sift::Reader::getStaticInstruction(uint64_t addr, uint8_t size)
{
   const StaticInstruction *sinst;

   // Lookup in a large unordered_map is quite expensive if we have to do this for every dynamic instruction
   // Therefore, keep a pointer to the probable next instruction in each (static) instruction
   if (m_last_sinst && m_last_sinst->next && m_last_sinst->next->addr == addr)
   {
      sinst = m_last_sinst->next;
   }
   else if (scache.count(addr))
   {
      sinst = scache[addr];
      assert(sinst->size == size);
   }
   else
   {
      sinst = staticInfoInstruction(addr, size);
      scache[addr] = sinst;
   }

   if (m_last_sinst && m_last_sinst->next == NULL)
   {
      ((StaticInstruction*)m_last_sinst)->next = sinst;
   }
   m_last_sinst = sinst;

   return sinst;
}

void Sift::Reader::sendSyscallResponse(uint64_t return_code)
{
   #if VERBOSE > 0
   std::cerr << "[DEBUG:" << m_id << "] Write SyscallResponse" << std::endl;
   #endif

   if (!initResponse())
   {
      std::cerr << "[SIFT:" << m_id << "] Error: initResponse failed\n";
   }

   Record rec;
   rec.Other.zero = 0;
   rec.Other.type = RecOtherSyscallResponse;
   rec.Other.size = sizeof(return_code);
   response->write(reinterpret_cast<char*>(&rec), sizeof(rec.Other));
   response->write(reinterpret_cast<char*>(&return_code), sizeof(return_code));
   response->flush();
}

void Sift::Reader::sendEmuResponse(bool handled, EmuReply res)
{
   #if VERBOSE > 0
   std::cerr << "[DEBUG:" << m_id << "] Write sendEmuResponse" << std::endl;
   #endif

   if (!initResponse())
   {
      std::cerr << "[SIFT:" << m_id << "] Error: initResponse failed\n";
   }

   Record rec;
   rec.Other.zero = 0;
   rec.Other.type = RecOtherEmuResponse;
   rec.Other.size = sizeof(uint8_t) + sizeof(EmuReply);
   uint8_t result = handled;
   response->write(reinterpret_cast<char*>(&rec), sizeof(rec.Other));
   response->write(reinterpret_cast<char*>(&result), sizeof(uint8_t));
   response->write(reinterpret_cast<char*>(&res), sizeof(EmuReply));
   response->flush();
}

void Sift::Reader::sendSimpleResponse(RecOtherType type, void *data, uint32_t size)
{
   #if VERBOSE > 0
   std::cerr << "[DEBUG:" << m_id << "] Write SimpleResponse type=" << type << std::endl;
   #endif

   if (!initResponse())
   {
      std::cerr << "[SIFT:" << m_id << "] Error: initResponse failed\n";
   }

   Record rec;
   rec.Other.zero = 0;
   rec.Other.type = type;
   rec.Other.size = size;
   response->write(reinterpret_cast<char*>(&rec), sizeof(rec.Other));
   if (size > 0)
   {
      response->write(reinterpret_cast<char*>(data), size);
   }
   response->flush();
}

uint64_t Sift::Reader::getPosition()
{
   if (inputstream)
      return inputstream->tellg();
   else
      return 0;
}

uint64_t Sift::Reader::getLength()
{
   return filesize;
}

uint64_t Sift::Reader::va2pa(uint64_t va)
{
   if (m_trace_has_pa)
   {
      intptr_t vp = va / PAGE_SIZE_SIFT;
      intptr_t vo = va & (PAGE_SIZE_SIFT-1);

      if (vcache.count(vp) == 0)
      {
         return 0;
      }
      else
      {
         intptr_t pp = vcache[vp];
         return (pp * PAGE_SIZE_SIFT) | vo;
      }
   }
   else
   {
      return va;
   }
}

void Sift::Reader::frontEndStop()
{
    if(response)
	sendSimpleResponse(RecOtherType::RecOtherShutdown, NULL, 0);
}

