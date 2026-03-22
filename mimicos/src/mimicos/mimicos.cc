#include <iostream>
#include <string>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <semaphore.h>
#include <memory> 
#include <cstring>  // Include this to use strcpy and strncpy
#include <cassert>  // Include this to use strcpy and strncpy

#include "mimicos.h"
#include "mm/allocator_factory.h"
#include "sim_api.h"
#include "globals.h"
#include "fixed_types.h"

#include "debug_config.h"


#define SIM

#define BASE_PAGE_SHIFT 12UL

#define MAX_ARGS 10
INIReader *reader;        // defined in globals.h
MetricsRegistry *m_stats; // defined in globals.h

int result_counter = 0;

MimicOS::MimicOS(std::string configurationFile, std::string outputFile, std::string appFile)
    : path_to_outputFile(outputFile), 
      path_to_app(appFile), 
      path_to_configFile(configurationFile)

{

    // Initialize configuration parser
    reader = new INIReader(configurationFile);

    // Initialize metrics registry
    m_stats = new MetricsRegistry();


    //Create physical memory allocator based on config file
    // Read configuration parameters from the INI file
    String allocatorName = reader->Get("allocator", "memory_allocator", "").c_str();

    //max_order is only applicable to Buddy-based allocators (e.g., ReserveTHP, Baseline)
    int maxOrder = reader->GetInteger("allocator", "max_order", 0);
    std::cout << "[MimicOS]: Allocator order: " << maxOrder << std::endl;

    //by setting the kernel size, we reserve the first 'kernel_size' MB for the kernel
    int kernelSize = reader->GetInteger("pmem_alloc"    , "kernel_size", 0);
    std::cout << "[MimicOS]: Kernel size: " << kernelSize << std::endl;

    //Fragmentation's definition depends on what we want to optimize for
    // "contiguity" => we want to optimize for contiguity (i.e., average block size)
    // "large_pages" => we want to optimize for large pages (i.e., ratio of 2MB pages)
    String fragType = reader->Get("pmem_alloc", "frag_type", "none").c_str();
        
    int memory_size = reader->GetInteger("pmem_alloc", "memory_size", 0);
    std::cout << "[MimicOS]: Memory size: " << memory_size << std::endl;

    //threshold_for_promotion is only applicable to ReserveTHP allocator
    // As we saw in Part1.1, we do not promote a 2MB region to a full 2MB page unless
    // the fraction of used 4KB pages in that region exceeds threshold_for_promotion
    int threshold_for_promotion = reader->GetInteger("pmem_alloc", "threshold_for_promotion", -1);
    std::cout << "[MimicOS]: Threshold for promotion (Applicable to ReserveTHP, otherwise: default = -1): " << threshold_for_promotion << std::endl;

    //Create physical memory allocator - this will be used to serve page allocations
    // throughout the execution of the SIFT-based application
    physical_memory_allocator = AllocatorFactory::createAllocator(allocatorName, memory_size, maxOrder, kernelSize, fragType, threshold_for_promotion);
    std::cout << "[MimicOS]: Created allocator: " << allocatorName << std::endl;
}

void MimicOS::boot()
{
    double target_fragmentation = reader->GetReal("pmem_alloc"    , "target_fragmentation", 1.0);
    std::cout << "[MimicOS]: Fragmenting memory w/ target_fragmentation factor = " << target_fragmentation << std::endl;

    //Fragment memory to achieve target fragmentation
    // This is optional, depending on the configuration parameter 'target_fragmentation'
    // If target_fragmentation = 1.0 => no fragmentation is applied
    // If target_fragmentation < 1.0 => fragmentation is applied to achieve the target
    physical_memory_allocator->fragment_memory(target_fragmentation);


    //CRITICAL: MimicOS starts like an actual OS, we need to spawn a process
    // In our scenario, to simplify things, we spawn a single process but providing the path to an existing trace
    std::cout << "[MimicOS]: Calling start_application" << std::endl;
    start_application();

    std::cout << "[MimicOS]: Calling poll_for_signal" << std::endl;
    poll_for_signal();
}


void MimicOS::initHandlers() {

}


void MimicOS::start_application()
{
    const char *path = path_to_app.c_str();
    std::cout << "[MimicOS] [start_application]: path = " << path << std::endl;

    //Execute an instruction which we call "magic"
    // r11 = *pointer_to_file OR opcode
    std::cout << "[MimicOS] [start_application]: Before SimStartProcess" << std::endl;
    SimRoiStart();

    //Set up the magic instruction, the simulator will intercept it and start executing the SIFT-based application

    SimStartProcess((long unsigned int)(path));

    std::cout << "[MimicOS] [start_application]: After SimStartProcess" << std::endl;
}


/**
 * @brief Polls for signals from the SIFT-based application and handles memory allocation requests.
 * 
 * This function implements the main message handling loop for MimicOS. It performs an initial
 * context switch to halt and wait for the SIFT-based application to send memory allocation
 * requests (typically triggered by page faults). The function continuously receives messages,
 * processes memory allocation requests, and sends responses back to the application.
 * 
 * Message Protocol:
 * - Incoming: [exception_type_code, virtual_address, num_requested_frames, ...]
 * - Outgoing: [exception_type_code, vpn, physical_address, page_size, frame1, frame2, ...]
 * 
 * @details The function:
 * 1. Performs initial context switch to synchronize with SIFT application
 * 2. Enters infinite loop to handle incoming memory requests
 * 3. Decodes exception type and extracts virtual page number (VPN)
 * 4. Allocates requested number of physical memory frames (data + page table frames)
 * 5. Sends allocation results back to the requesting application
 * 6. Performs context switch to return control to the application
 * 
 * @note This function runs indefinitely and should be called from the main MimicOS thread.
 * @note Fatal error occurs if physical memory allocation fails.
 * 
 * @see SimContextSwitch(), SimReceiveMessage(), SimMimicosResult()
 * @see PhysicalMemoryAllocator::allocate(), PhysicalMemoryAllocator::handle_page_table_allocations()
 */

void MimicOS::poll_for_signal()
{
    // Keep the message structure to receive messages and keep it alive and as simple as possible
    Message* msg = new Message;
    msg->argv = new uint64_t[10];

    //Initial context switch to halt and wait for the SIFT-based application to send a message
    // Sniper will only send a message when the SIFT-based application triggers a page fault
    SimContextSwitch();

    std::cout << "[MimicOS] [poll_for_signal]: Continuing after context switch." << std::endl;

    while (true) {

        //Receive message from the SIFT-based application
        // The message contains the exception type, virtual address, and number of requested frames
        // We reuse the same message structure to keep things simple
        // msg->argv[0] = exception_type_code
        // msg->argv[1] = virtual_address
        // msg->argv[2] = num_requested_frames (data frame + page table frames)
        // msg->argv[3..] = unused

        SimReceiveMessage(&msg->argc, msg->argv);
        assert(msg->argc >= 2);
        int exception_type_code = msg->argv[0];
        std::string exception_type = std::to_string(exception_type_code);
        IntPtr va = msg->argv[1];
        IntPtr vpn = (va >> BASE_PAGE_SHIFT);
        int num_requested_frames = msg->argv[2];
        
#if DEBUG_MimicOS >= DEBUG_BASIC
        std::cout << "[MimicOS] Received response from context switch ..." << std::endl;
        std::cout << "[MimicOS] Message is exception_type = " << exception_type <<
                     " -- vpn = " << vpn <<
                     "- - num_requested_frames = " << num_requested_frames << std::endl;
#endif 

        #ifdef PROTOCOL 
            assert(argc >= 1);
            std::string message_type;
            if (protocol_codes_decode.find(argv[0]) == protocol_codes_decode.end()) {
                std::cout << "[MimicOS]: Unknown protocol code: " << argv[0] << std::endl;
                continue; // Skip unknown messages
            }
            else {
                message_type = protocol_codes_decode[argv[0]];
                std::cout << "[MimicOS]: protocol name / message type : " << message_type << std::endl;
            }
        #endif
        
        UInt64 bytes = (1 << 12);

        //For simplicity, we assume core_id = 0 for now
        // In a more complex scenario, we could extract core_id from the message or maintain per-core state
        // This would be useful in a multi-core simulation where different cores may have different memory access patterns
        // and we want to track allocations per core
        // For now, we keep it simple and use core_id = 0
        auto core_id = 0;

        //Allocate the requested number of frames
        // The first frame is the data frame, the rest are page table frames
        // We use the physical memory allocator created during MimicOS initialization
        // If allocation fails, we print an error and exit
        // In a real OS, we would handle this more gracefully (e.g., by killing the process or freeing up memory)
        // Here, we simply exit to keep the example straightforward

        std::vector<UInt64> frames;
        frames.reserve(num_requested_frames);

        //Allocate the data frame first by asking for 'bytes' (4KB)
        // The physical_memory_allocator->allocate() returns a pair of <physical_page, page_size>

        auto [pa, page_size] = physical_memory_allocator->allocate(bytes, va, core_id);
        if (pa == static_cast<UInt64>(-1)) {
            std::cerr << "[FATAL] [MimicOS] No more memory available to sustain this memory allocation" << std::endl;
            std::cerr << "[FATAL] [MimicOS] Exiting..." << std::endl;
            exit(1);
        }

        //Store the allocated physical address of the data frame    
        frames.push_back(pa);
        // Allocate page table frames
        for (int i = 0; i < num_requested_frames - 1; i++) {
            auto frame = physical_memory_allocator->handle_page_table_allocations(bytes);
#if DEBUG_MimicOS >= DEBUG_BASIC
            std::cout << "[MimicOS] Allocated page table frame " << i << ": " << frame << std::endl;
#endif
            frames.push_back(frame);
        }

#if DEBUG_MimicOS >= DEBUG_BASIC
            std::cout << "[MimicOS] Physical memory allocation succeeded: " << std::endl;
            std::cout << "[MimicOS] Number of frames requested = " << num_requested_frames << std::endl;
            std::cout << "[MimicOS] Frames allocated: ";
            for (int i = 0; i < num_requested_frames; i++) {
                if (i == 0) {
                    std::cout << "Data frame: ";
                } else {
                    std::cout << "Page table frame " << (i - 1) << ": ";
                }
                std::cout << frames[i] << std::endl;
            }
#endif
        //Send the allocation results back to the SIFT-based application
        // We reuse the same message structure to keep things simple
        // msg->argv[0] = exception_type_code (same as received)
        // msg->argv[1] = vpn (virtual page number)
        // msg->argv[2] = pa (physical address of the data frame)
        // msg->argv[3] = page_size (size of the data frame in bits, typically 12 for 4KB)
        // msg->argv[4..] = frames[i] (physical addresses of allocated frames)
        // msg->argc = 4 + num_requested_frames
        // Reuse the message structure to send a response back to MimicOS
        msg->argc = 4 + num_requested_frames; // 4 for exception_type_code, vpn, pa, page_size + num_requested_frames for frames

        msg->argv[0] = exception_type_code;
        msg->argv[1] = vpn;
        msg->argv[2] = pa;
        msg->argv[3] = page_size;
        for (int i = 0; i < num_requested_frames; i++) {
            msg->argv[4 + i] = frames[i];
        }

        
        //Send the response back to the SIFT-based application using a magic instruction
        // The application will receive the physical addresses and map them accordingly

        SimMimicosResult(msg->argc, msg->argv);
         
        //Perform context switch to return control to the SIFT-based application
        // The application will continue executing after receiving the allocation results
        SimContextSwitch();
    }

}


// Define the static member variable
MimicOS* MimicOS::instance = nullptr;