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

#ifndef DCFG_TRACE_API_H
#define DCFG_TRACE_API_H

#include "dcfg_api.H"

namespace dcfg_trace_api {

    /**
     * Interface to all data in a DCFG edge trace.
     * This is an interface; use DCFG_TRACE::new_trace()
     * to create an object that implements the interface.
     */
    class DCFG_TRACE_READER {
    public:

        // Default virtual destructor to ensure cleanup.
        virtual ~DCFG_TRACE_READER() {}

        /**
         * Create a new DCFG edge-trace reader.
         * This is a factory method to create a new
         * object that implements the DCFG_TRACE_READER interface.
         * A reader can access only one process.
         * Create multiple readers to read multiple processes.
         * @return Pointer to new object. It can be freed
         * with `delete`.
         */
        static DCFG_TRACE_READER*
        new_reader(dcfg_api::DCFG_ID process_id
                   /**< [in] ID of process to read.
                      This can be determined from the DCFG data
                      object corresponding to the trace. */
                   );

        /**
         * Open a file for reading from the given thread.
         * @return `true` on success, `false` otherwise (and sets `errMsg`).
         */
        virtual bool
        open(const std::string filename,
             /**< [in] Name of file to open, which
                must follow the DCFG-Trace JSON format. */
             UINT32 tid,
             /**< [in] ID number of thread to read. */
             std::string& errMsg
             /**< [out] Contains error message upon failure. */
             ) =0;

        /**
         * Read a chunk of edge IDs.
         * They will be added to `edge_ids` in the order in
         * which they were recorded.
         * This method will not typically read all the values at once.
         * Call it repeatedly until `done` is set to `true`.
         * @return `true` on success, `false` otherwise (and sets `errMsg`).
         */
        virtual bool
        get_edge_ids(dcfg_api::DCFG_ID_CONTAINER &edge_ids,
                     /**< [out] Container to which edge IDs are added.
                        Previous contents of the container are *not*
                        emptied by this call, so it should be emptied by
                        the programmer before the call if desired.  The
                        programmer can use any implementation of
                        DCFG_ID_CONTAINER that maintains insertion order:
                        DCFG_ID_VECTOR, etc. */
                     bool& done,
                     /**< [out] Set to `true` when end of sequence has
                        been reached, `false` if there are more to read. */
                     std::string& errMsg
                     /**< [out] Contains error message upon failure. */
                     ) =0;
    };
};                           // namespace
#endif
