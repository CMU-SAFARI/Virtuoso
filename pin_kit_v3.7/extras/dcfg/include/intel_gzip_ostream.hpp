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

/** Create a custom output stream by overriding streambuf's overflow method
    to use gzip to stream into a compressed gz file. */

#include <iostream>
#include "zlib.h"

// custom output streambuf that can write to gz compressed files, overwrites existing file
class intel_gzip_ostreambuf : public std::streambuf {
public:
    intel_gzip_ostreambuf(const std::string &src, const int sBufferOutputSize);

    ~intel_gzip_ostreambuf();

    bool constructionCompleted(void);

private:
    gzFile gzFilePointer;
    std::string fileName;
    char *streamOutputBuffer;
    int streamOutputBufferIndex;
    int error;
    bool constructionComplete;

    bool CloseFile();
    bool openGzFileForWrite();
    const int streamOutputBufferSize;

protected:
    // redirect 1 character at a time to bzip compressed file
    virtual int_type overflow(int_type c);
};


/** Create a custom output stream for streaming into a gz compressed file.
    This is achieved by constructing a custom streambuf (intel_gzip_ostreambuf) that writes to gz files.
    Then creating an object inheriting from ostream and setting its streambuffer (intel_gzip_ostream)
    to use the custom streambuf.
*/
class intel_gzip_ostream : public std::ostream{
protected:
    intel_gzip_ostreambuf ob;
public:
    intel_gzip_ostream(std::string name, const int sBufferOutputSize) : 
        std::ostream(0), ob(name, sBufferOutputSize){
        rdbuf(&ob);
    }

    bool constructorSuccessful(void){
        return ob.constructionCompleted();
    }

};
