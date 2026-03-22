#include "mimicos_message.h"

// Static member definitions
std::unordered_map<std::string, uint64_t> MimicOSProtocol::s_encode_map = {
    {"page_fault", MimicOSProtocol::PAGE_FAULT},
    {"syscall", MimicOSProtocol::SYSCALL}
};

std::unordered_map<uint64_t, std::string> MimicOSProtocol::s_decode_map = {
    {MimicOSProtocol::PAGE_FAULT, "page_fault"},
    {MimicOSProtocol::SYSCALL, "syscall"}
};
