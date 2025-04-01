#ifndef __ZFSTREAM_H
#define __ZFSTREAM_H

#include "sift_format.h"

#include <ostream>
#include <istream>
#include <fstream>

#if SIFT_USE_ZLIB
# include <zlib.h>
#endif

class vostream
{
   public:
      virtual ~vostream() {}
      virtual void write(const char* s, std::streamsize n) = 0;
      virtual void flush() = 0;
      virtual bool is_open() = 0;
      virtual bool fail() = 0;
};

class vofstream : public vostream
{
   private:
      std::ofstream *stream;
   public:
      vofstream(const char * filename, std::ios_base::openmode mode = std::ios_base::out)
         : stream(new std::ofstream(filename, mode)) {}
      vofstream(std::ofstream *stream)
         : stream(stream) {}
      virtual ~vofstream() { delete stream; }
      virtual void write(const char* s, std::streamsize n)
         { stream->write(s, n); }
      virtual void flush()
         { stream->flush(); }
      virtual bool fail()
         { return stream->fail(); }
      virtual bool is_open()
         { return stream->is_open(); }
};

class ozstream : public vostream
{
   private:
      vostream *output;
#if SIFT_USE_ZLIB
      z_stream zstream;
#endif
      static const size_t chunksize = 64*1024;
      static const int level = 9;
      char buffer[chunksize];
      void doCompress(bool finish);
   public:
      ozstream(vostream *output);
      virtual ~ozstream();
      virtual void write(const char* s, std::streamsize n);
      virtual void flush()
         { output->flush(); }
      virtual bool fail()
         { return output->fail(); }
      virtual bool is_open()
         { return output->is_open(); }
};



class vistream
{
   public:
      virtual ~vistream() {}
      virtual void read(char* s, std::streamsize n) = 0;
      virtual int peek() = 0;
      virtual bool fail() const = 0;
};

class vifstream : public vistream
{
   private:
      std::ifstream *stream;
   public:
      vifstream(const char * filename, std::ios_base::openmode mode = std::ios_base::in)
         : stream(new std::ifstream(filename, mode)) {}
      vifstream(std::ifstream *stream)
         : stream(stream) {}
      virtual ~vifstream() { delete stream; }
      virtual void read(char* s, std::streamsize n)
         { stream->read(s, n); }
      virtual int peek()
         { return stream->peek(); }
      virtual bool fail() const { return stream->fail(); }
};

class cvifstream : public vistream
{
   private:
	   std::FILE *stream;
	   char peek_buffer;
	   bool buffer_in_use;
   public:
	   cvifstream(const char * filename, std::ios_base::openmode mode = std::ios_base::in);
	   cvifstream(std::FILE *stream)
		   : stream(stream) {}
	   virtual ~cvifstream();
	   virtual void read(char* s, std::streamsize n);
	   virtual int peek();
	   virtual bool fail() const;
};

class izstream : public vistream
{
   private:
      vistream *input;
      bool m_eof;
      bool m_fail;
#if SIFT_USE_ZLIB
      z_stream zstream;
#endif
      static const size_t chunksize = 64*1024;
      char buffer[chunksize];
      char peek_value;
      bool peek_valid;
   public:
      izstream(vistream *input);
      virtual ~izstream();
      virtual void read(char* s, std::streamsize n);
      virtual int peek();
      virtual bool eof() const { return m_eof; }
      virtual bool fail() const { return m_fail; }
};

#endif // __ZFSTREAM_H
