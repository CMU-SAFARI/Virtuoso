#ifndef __SIFT_UTILS_H
#define __SIFT_UTILS_H

#include "sift.h"

namespace Sift
{
   // TODO: @vlnitu Discuss with @konkanello if we want to move this structure in a common place; currently it's code duplicated 
   // sift_utils.h & mimicos.h
   struct Message{
      int argc;
      uint64_t *argv;
   };
   void hexdump(const void * data, uint32_t size);
};

#endif // __SIFT_UTILS_H
