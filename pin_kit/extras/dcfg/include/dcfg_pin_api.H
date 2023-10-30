/*BEGIN_LEGAL 
BSD License 

Copyright (c) 2015-2016 Intel Corporation. All rights reserved.
 
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

#ifndef DCFG_PIN_API_H
#define DCFG_PIN_API_H

#include "pin.H"
#include "dcfg_api.H"

namespace dcfg_pin_api {

    /**
     * Connection between a Pin tool and a DCFG_DATA object.
     * This is an interface; use DCFG_PIN_MANAGER::new_manager()
     * to create an object that implements the interface.
     */
    class DCFG_PIN_MANAGER {
    public:

        // Default virtual destructor to ensure cleanup.
        virtual ~DCFG_PIN_MANAGER() {}

        /**
         * Create a new DCFG_PIN_MANAGER. This is a factory method to create
         * a new object that implements the DCFG_PIN_MANAGER interface.
         * @return Pointer to new object. It can be freed with `delete`.
         */
        static DCFG_PIN_MANAGER* new_manager();

        /**
         * Whether the '-dcfg' knob was used on the command-line.
         * @return `true` if '-dcfg' knob was used, `false` otherwise.
         */
        virtual bool dcfg_enable_knob() const;

        /**
         * Initialize and add Pin instrumentation.  Default behavior depends
         * on settings of dcfg knobs.
         */
        virtual void activate() =0;

        /**
         * Initialize and add PinPlay instrumentation.  Default behavior
         * depends on settings of dcfg knobs and whether logger, replayer or
         * both are activated.
         */
        virtual void activate(void * pinplay_engine
                              /**< [in] pointer to existing pinplay engine
                                 or `NULL` if none. */
                              ) =0;

        /**
         * Get access to DCFG data constructed
         * by the Pin tool.
         * The returned DCFG will only be valid
         * at the end of a region or program.
         * There will not be a DCFG if the activate() method
         * has not been called.
         * @return Pointer to associated DCFG data or `NULL` if none.
         */
        virtual dcfg_api::DCFG_DATA_CPTR get_dcfg_data() const =0;
        
        /**
         * Explicitly set CFG-data collection.  This controls whether
         * control-flow instructions are instrumented to build a CFG.  This
         * is independent of whether a DCFG file is written.  If a DCFG file
         * is written with CFG collection disabled, it will have no CFG data
         * in it.
         */
        virtual void
        set_cfg_collection(bool enable
                           /**< [in] turn CFG collection on or off. */
                           ) =0;
    };

};                           // namespace
#endif
