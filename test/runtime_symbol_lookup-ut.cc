#include <gtest/gtest.h>

#include "logger.hpp"
#include "loghandle.hpp"

static const char *const s_addr_line = "00007FBCD9B44740 23e instance void [Samples.Computer01] Samples.Computer01.PiComputation::DoPiComputation()[Optimized]";

TEST(runtime_symbol_lookup, scan_func) { 
    LogHandle log_handle;
    uint64_t address;
    uint32_t code_size;
    char buffer[2048];
    static const char spec[] = "%lx %x %[^\t\n]";
    int res_scan = sscanf(s_addr_line, spec, &address, &code_size, buffer);
    LG_DBG("Scanned: %lx - %u - %s - res=%d", address, code_size, buffer, res_scan);
    
    // std::cerr << res_scan << " " << address << " " << code_size << " " << buffer << std::endl;
 }
