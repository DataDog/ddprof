#include <cstdint>
#include <cstdio>

static bool init() {
  printf("Dummy init !!!\n");
  return true;
}

static bool initialized = init();
static uint64_t s_return_address;

extern "C" void EntryPayload(uint64_t return_address, uint64_t function_id,
                             uint64_t stack_pointer,
                             uint64_t return_trampoline_address) {
  uint64_t *stackp = reinterpret_cast<uint64_t *>(stack_pointer);
  printf("EntryHook: function_id=%lu, arg1=0x%lx, arg2=0x%lx\n", function_id,
         stackp[-5], stackp[-4]);
  s_return_address = return_address;
  *stackp = return_trampoline_address;
}

extern "C" uint64_t ExitPayload() {
  printf("ExitHook\n");
  return s_return_address;
}
