// <COMPONENT>: os-apis
// <FILE-TYPE>: component public header

#ifndef OS_APIS_TLS_ENUM_H_
#define OS_APIS_TLS_ENUM_H_

// Well-known TLS slots. What data goes in which slot is arbitrary unless otherwise noted.
enum
{
    TLS_SLOT_SELF = 0,
    TLS_SLOT_THREAD_ID,
    TLS_SLOT_ERRNO,
    // This slot is only used to pass information from the dynamic linker to
    // libc.so when the C library is loaded in to memory. The C runtime init
    // function will then clear it. Since its use is extremely temporary,
    // we reuse an existing location that isn't needed during libc startup.
    TLS_SLOT_BIONIC_PREINIT = 3,
    TLS_SLOT_TLS_AND_STACK_SIZE,
    TLS_SLOT_TLS_AND_STACK_START_ADDRESS,
    TLS_SLOT_PIN_RESERVED, // Reserved for Pin
    TLS_SLOT_DLERROR,
    TLS_SLOT_DLERROR_BUFFER,
    TLS_SLOT_OPEN_FDS, //Slots for files opened by Pin in thread.
    TLS_SLOT_FIRST_USER_SLOT, //Must be last
};

#endif // file guard
