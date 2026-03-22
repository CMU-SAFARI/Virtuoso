#ifndef PAGE_FAULT_STATE_H
#define PAGE_FAULT_STATE_H

#include "fixed_types.h"

/**
 * @brief Page fault state for communication between MMU and exception handlers
 * 
 * This struct tracks the state of a page fault during handling, particularly
 * for userspace MimicOS communication where the fault needs to be replayed
 * after a context switch.
 */
struct PageFaultState {
    bool is_active;              ///< True if a page fault is currently being handled
    IntPtr faulting_va;          ///< Virtual address that triggered the page fault
    int num_requested_frames;    ///< Number of frames requested for page fault handling
    
    /**
     * @brief Default constructor - initializes to inactive state
     */
    PageFaultState() 
        : is_active(false)
        , faulting_va(static_cast<IntPtr>(-1))
        , num_requested_frames(-1) 
    {}
    
    /**
     * @brief Reset the page fault state to inactive
     */
    void reset() {
        is_active = false;
        faulting_va = static_cast<IntPtr>(-1);
        num_requested_frames = -1;
    }
    
    /**
     * @brief Set the page fault state as active
     * 
     * @param va The virtual address that triggered the fault
     * @param frames Number of frames requested for handling
     */
    void setActive(IntPtr va, int frames) {
        is_active = true;
        faulting_va = va;
        num_requested_frames = frames;
    }
    
    /**
     * @brief Check if the state is active
     */
    bool isActive() const { return is_active; }
    
    /**
     * @brief Get the faulting virtual address
     */
    IntPtr getFaultingVA() const { return faulting_va; }
    
    /**
     * @brief Get the number of requested frames
     */
    int getNumRequestedFrames() const { return num_requested_frames; }
};

#endif // PAGE_FAULT_STATE_H
