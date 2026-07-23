#include <stdio.h>
#include <stdlib.h>
#include <stdint.h> // For uint64_t
#include <fcntl.h>  // For open
#include <unistd.h> // For sysconf, lseek, read, close
#include <errno.h>  // For errno
#include <string.h> // For strerror

// --- Configuration ---
#define ALLOCATION_SIZE (1024 * 1024) // Allocate 1 MB
// -------------------

// Helper function to get page size
long get_page_size() {
    long page_size = sysconf(_SC_PAGESIZE);
    if (page_size == -1) {
        perror("sysconf failed to get page size");
        return -1;
    }
    return page_size;
}

// Function to parse pagemap entry and get PFN
// Returns PFN if present, 0 otherwise (or if permissions insufficient)
// Also sets 'present' flag accordingly
uint64_t get_pfn(uint64_t pagemap_entry, int *present) {
    *present = 0;
    // Check present bit (bit 63)
    if ((pagemap_entry >> 63) & 1) {
        *present = 1;
        // Extract PFN (bits 0-54)
        uint64_t pfn = pagemap_entry & ((1ULL << 55) - 1);
        return pfn;
    }
    // Check swapped bit (bit 62) - optional, just returning not present here
    // if ((pagemap_entry >> 62) & 1) { ... }
    return 0; // Not present or swapped
}


int main(int argc, char *argv[]) {
    char *memory_block = NULL;
    FILE *outfile = NULL;
    int pagemap_fd = -1;
    long page_size = -1;

    printf("Microbenchmark: Virtual to Physical Address Mapping\n");
    printf("NOTE: This program likely requires root privileges or CAP_SYS_ADMIN capability\n");
    printf("      to read physical address information from /proc/self/pagemap.\n");

    // 1. Get Page Size
    page_size = get_page_size();
    if (page_size <= 0) {
        fprintf(stderr, "Error: Could not determine page size.\n");
        return EXIT_FAILURE;
    }
    printf("System Page Size: %ld bytes\n", page_size);

    // 2. Allocate Memory
    memory_block = (char *)malloc(ALLOCATION_SIZE);
    if (memory_block == NULL) {
        perror("malloc failed");
        return EXIT_FAILURE;
    }
    printf("Allocated %d bytes at virtual address: %p\n", ALLOCATION_SIZE, memory_block);

    // 3. Touch Memory (Read/Write to ensure pages are mapped)
    printf("Touching allocated memory to ensure mapping...\n");
    for (size_t i = 0; i < ALLOCATION_SIZE; i += page_size) {
        memory_block[i] = (char)(i % 256); // Write to first byte of each page
    }
    printf("Memory touched.\n");

    char* output_filename = argv[1];
    // 4. Open output file
    outfile = fopen(output_filename, "w");
    if (outfile == NULL) {
        perror("fopen failed for output file");
        free(memory_block);
        return EXIT_FAILURE;
    }
    fprintf(outfile, "Virtual Address -> Physical Address Mapping (Page Size: %ld)\n", page_size);
    fprintf(outfile, "===========================================================\n");
    fprintf(outfile, "Note: Physical Address = (PFN * PageSize) + Offset\n");
    fprintf(outfile, "Note: PFN=0 or 'Not Present' might indicate insufficient permissions.\n\n");


    // 5. Open /proc/self/pagemap
    // This requires read access and specific permissions/capabilities on modern kernels.
    pagemap_fd = open("/proc/self/pagemap", O_RDONLY);
    if (pagemap_fd < 0) {
        perror("Failed to open /proc/self/pagemap. insufficient permissions?");
        fclose(outfile);
        free(memory_block);
        return EXIT_FAILURE;
    }
    printf("Opened /proc/self/pagemap successfully.\n");

    // 6. Iterate through pages and get mappings
    printf("Querying pagemap and writing to %s...\n", output_filename);
    for (size_t i = 0; i < ALLOCATION_SIZE; i += page_size) {
        uintptr_t vaddr = (uintptr_t)memory_block + i;
        uint64_t pagemap_entry;
        off_t offset;

        // Calculate offset in pagemap file: (VPN * entry_size)
        // VPN = virtual_address / page_size
        offset = (vaddr / page_size) * sizeof(uint64_t);

        // Seek to the correct entry
        if (lseek(pagemap_fd, offset, SEEK_SET) == (off_t)-1) {
            fprintf(stderr, "lseek failed for VA %p (offset %ld): %s\n", (void*)vaddr, (long)offset, strerror(errno));
            continue; // Skip this page
        }

        // Read the 64-bit pagemap entry
        ssize_t read_bytes = read(pagemap_fd, &pagemap_entry, sizeof(uint64_t));
        if (read_bytes < 0) {
             fprintf(stderr, "read failed for VA %p: %s\n", (void*)vaddr, strerror(errno));
             continue; // Skip this page
        }
        if (read_bytes != sizeof(uint64_t)) {
            fprintf(stderr, "Short read for VA %p (expected %zu, got %zd bytes)\n",
                    (void*)vaddr, sizeof(uint64_t), read_bytes);
            continue; // Skip this page
        }

        // Parse the entry
        int present = 0;
        uint64_t pfn = get_pfn(pagemap_entry, &present);

        // Write to output file
        fprintf(outfile, "VA: 0x%016lx -> ", vaddr);
        if (present) {
            // Calculate physical address: (PFN * Page Size) + (Virtual Address % Page Size)
            uint64_t paddr = (pfn * page_size) + (vaddr % page_size);
            fprintf(outfile, "PA: 0x%016lx (PFN: 0x%lx)\n", paddr, pfn);
        } else {
            // Check if swapped (bit 62) - basic check
            if ((pagemap_entry >> 62) & 1) {
                 fprintf(outfile, "Swapped Out\n");
            } else {
                 fprintf(outfile, "Not Present (PFN: 0x%lx)\n", pfn); // PFN might be 0 due to permissions
            }
        }
    }

    printf("Finished writing mappings to %s.\n", output_filename);

    // 7. Cleanup
    close(pagemap_fd);
    fclose(outfile);
    free(memory_block);

    printf("Microbenchmark finished.\n");

    return EXIT_SUCCESS;
}