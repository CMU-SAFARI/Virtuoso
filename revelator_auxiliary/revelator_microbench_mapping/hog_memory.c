#include <stdio.h>
#include <stdlib.h>
#include <string.h> // For memset
#include <unistd.h> // For sysconf

// --- Configuration ---
// Adjust these values based on your system RAM and desired fragmentation level
// Be cautious with very large values that might exhaust system memory!
#define CHUNK_SIZE_1 (1 * 1024 * 1024)     // 1 MB chunks for initial allocation
#define NUM_ALLOCS_1 2048                  // Allocate 2048 * 1MB = 2GB initially
#define CHUNK_SIZE_2 (CHUNK_SIZE_1 * 4)  // 4 MB chunks for secondary allocation (larger)
#define NUM_ALLOCS_2 (NUM_ALLOCS_1 / 4)    // Try to allocate 1/4 as many, but larger chunks

// How many bytes to skip between writes when touching memory
// Should be less than page size to ensure each page is touched.
#define TOUCH_STRIDE 4096 
// -------------------

// Helper function to touch memory pages
void touch_memory(char *mem, size_t size) {
    if (mem == NULL) return;
    // Write the first byte of roughly every page
    for (size_t i = 0; i < size; i += TOUCH_STRIDE) {
        mem[i] = (char)(i % 256);
    }
    // Ensure the last byte is touched too if size isn't multiple of stride
    if (size > 0) {
         mem[size - 1] = (char)(size % 256);
    }
}

int main() {
    char **allocations1 = NULL;
    char **allocations2 = NULL;
    int success_count = 0;
    int failure_count = 0;

    printf("Memory Fragmentation Test Program\n");
    printf("Phase 1: Initial Allocation (%d chunks of %d bytes = %.2f MB total)\n",
           NUM_ALLOCS_1, CHUNK_SIZE_1, (double)NUM_ALLOCS_1 * CHUNK_SIZE_1 / (1024.0 * 1024.0));

    allocations1 = (char **)malloc(NUM_ALLOCS_1 * sizeof(char *));
    if (allocations1 == NULL) {
        perror("Failed to allocate array for pointers (Phase 1)");
        return EXIT_FAILURE;
    }
    memset(allocations1, 0, NUM_ALLOCS_1 * sizeof(char *)); // Initialize to NULL

    // --- Phase 1: Allocate initial chunks ---
    for (int i = 0; i < NUM_ALLOCS_1; ++i) {
        allocations1[i] = (char *)malloc(CHUNK_SIZE_1);
        if (allocations1[i] == NULL) {
            fprintf(stderr, "ERROR: malloc failed during Phase 1 at allocation #%d\n", i + 1);
            // Cleanup already allocated memory before exiting
            for (int j = 0; j < i; ++j) {
                free(allocations1[j]);
            }
            free(allocations1);
            return EXIT_FAILURE;
        }
        // Touch memory to ensure physical backing
        touch_memory(allocations1[i], CHUNK_SIZE_1);

        if ((i + 1) % 100 == 0) { // Print progress periodically
             printf("  Allocated %d / %d chunks...\n", i + 1, NUM_ALLOCS_1);
        }
    }
    printf("Phase 1: Initial allocation complete. All memory touched.\n\n");

    // --- Phase 2: Deallocate alternate chunks to create fragmentation ---
    printf("Phase 2: Deallocating alternate chunks...\n");
    for (int i = 0; i < NUM_ALLOCS_1; ++i) {
        // Free every other chunk (adjust pattern if needed)
        if (i % 2 == 0) {
            if (allocations1[i] != NULL) {
                free(allocations1[i]);
                allocations1[i] = NULL;
            }
        }
    }
    printf("Phase 2: Deallocation complete.\n");

    // --- Pause for Observation ---
    printf("\nMemory fragmented. Press Enter to attempt Phase 3 (large allocations)...\n");
    // Wait for user input before proceeding
    getchar();

    // --- Phase 3: Attempt larger allocations ---
    printf("\nPhase 3: Attempting secondary allocation (%d chunks of %d bytes = %.2f MB total)\n",
           NUM_ALLOCS_2, CHUNK_SIZE_2, (double)NUM_ALLOCS_2 * CHUNK_SIZE_2 / (1024.0 * 1024.0));

    allocations2 = (char **)malloc(NUM_ALLOCS_2 * sizeof(char *));
     if (allocations2 == NULL) {
        perror("Failed to allocate array for pointers (Phase 3)");
        // Fall through to cleanup phase 1 allocs
    } else {
        memset(allocations2, 0, NUM_ALLOCS_2 * sizeof(char *)); // Initialize to NULL
        for (int i = 0; i < NUM_ALLOCS_2; ++i) {
            allocations2[i] = (char *)malloc(CHUNK_SIZE_2);
            if (allocations2[i] == NULL) {
                // This failure is expected if fragmentation was successful
                printf("  Allocation #%d (size %d) FAILED (likely due to fragmentation).\n", i + 1, CHUNK_SIZE_2);
                failure_count++;
            } else {
                printf("  Allocation #%d (size %d) SUCCEEDED.\n", i + 1, CHUNK_SIZE_2);
                // Touch memory if successful
                touch_memory(allocations2[i], CHUNK_SIZE_2);
                success_count++;
            }
        }
        printf("Phase 3: Secondary allocation attempts complete. Success: %d, Failure: %d\n", success_count, failure_count);
    }


    printf("\nPhase 3: Press Enter to continue to cleanup...\n");
    getchar();
    // --- Phase 4: Cleanup ---
    printf("\nPhase 4: Cleaning up allocated memory...\n");
    // Free remaining Phase 1 allocations
    for (int i = 0; i < NUM_ALLOCS_1; ++i) {
        if (allocations1[i] != NULL) {
            free(allocations1[i]);
        }
    }
    free(allocations1);

    // Free successful Phase 3 allocations
    if(allocations2 != NULL) {
        for (int i = 0; i < NUM_ALLOCS_2; ++i) {
            if (allocations2[i] != NULL) {
                free(allocations2[i]);
            }
        }
        free(allocations2);
    }

    printf("Phase 4: Cleanup complete.\n");
    printf("Fragmentation test finished.\n");

    return EXIT_SUCCESS;
}