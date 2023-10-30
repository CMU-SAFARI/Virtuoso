/*BEGIN_LEGAL
  Intel Open Source License

  Copyright (c) 2016 Intel Corporation. All rights reserved.

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

#ifndef INTEL_ZIPSTREAM_H
#define INTEL_ZIPSTREAM_H

#include <iostream>

#define ISTREAM_BUFF_SIZE 0x1000000 // 1MB
#define OSTREAM_BUFF_SIZE 0x100 // 256B

/**
   The intel_zipstream namespace contains several functions and classes for working
   with files compressed with libz or libbz2 compression.
*/
namespace intel_zipstream {
    /**
       The compression policy selected.  Pass this in as an argument when opening a new stream for write.
    */
    enum CompressionPolicy {
        /** NoCompression implies just what it says - do not use compressed output streams. */
        NoCompression = 0,
        ZLibCompression = 1,
        GZipCompression = ZLibCompression,
        BZipCompression = 2,
        EndOfList
    };

    /**
       returns compression policies (gzip, bzip) supported
    */
    bool supports_compression_policy( CompressionPolicy c );


    /**
       Looks at the given file and tries to figure out what type
       of decompression stream, if any, should be used.
       It will return one of the following types:

       - std::ifstream
       - intel_zipstream::intel_gzip_ostream
       - intel_zipstream::intel_gzip_ostream

    */
    std::istream * get_istream( const std::string & src,
                                const CompressionPolicy compressionPolicy,
                                bool AsFile = true,
                                const int bufferSize = ISTREAM_BUFF_SIZE);

    /**
       Returns one of the following types of ostreams, depending
       on compression_policy() and compile-time library settings.

       - intel_zipstream::intel_bzip_istream
       - intel_zipstream::intel_gzip_istream
       - std::ofstream
    */
    std::ostream * get_ostream( const std::string & filename,
                                const CompressionPolicy compressionPolicy,
                                const int bufferSize = OSTREAM_BUFF_SIZE );

} // namespace intel_zipstream

#endif
