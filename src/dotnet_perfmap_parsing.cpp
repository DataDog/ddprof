#include <stdio.h>
#include <string>
#include <vector>
#include <iostream>
#include <string.h>

struct managed_method_info
{
	public:
    managed_method_info(std::uint64_t addr, std::size_t cs, std::string fn) :
	    address{addr},
	    code_size{cs},
	    function_name{std::move(fn)}
    {}

    std::uint64_t address;
    std::size_t code_size;
    std::string function_name;
};

static FILE* perfmaps_open(int pid, const char* path_to_perfmap = "") {
    char buf[1024] = { 0 };
    auto n = snprintf(buf, 1024, "%s/perf-%d.map", path_to_perfmap, pid);
    if (n >= 1024) { // unable to snprintf everything
        return nullptr;
    }
    return fopen(buf, "r");
}

bool should_skip_symbol(const char* symbol)
{
    return strstr(symbol, "GenerateResolveStub") != nullptr || 
           strstr(symbol, "GenerateDispatchStub") != nullptr || 
           strstr(symbol, "GenerateLookupStub") != nullptr || 
           strstr(symbol, "AllocateTemporaryEntryPoints") != nullptr;
}

std::vector<managed_method_info> parse_perfmaps_file(int pid)
{
    static const char spec[] = "%lx %x %[^\t\n]";
    FILE* pmf = perfmaps_open(pid, "/tmp");

    if (pmf == nullptr)
    {
       std::cout << "No perfmap file found for pid " << pid << std::endl;
       return {};
    }

    char* line = NULL;
    size_t sz_buf = 0;
    char buffer[2048];

    std::vector<managed_method_info> result;
    result.reserve(1024);

    while (-1 != getline(&line, &sz_buf, pmf))
    {
        uint64_t address;
        uint32_t code_size;
        if (3 != sscanf(line, spec, &address, &code_size, buffer) || should_skip_symbol(buffer))
        {
            continue;
        }
        result.emplace_back(address, code_size, std::string(buffer));
    }

    fclose(pmf);
    return result;
}

int main(int argc, char** argv)
{
   auto info = parse_perfmaps_file(42);
   std::cout << "Nb infos: " << info.size() << std::endl;
   return 0;
}
