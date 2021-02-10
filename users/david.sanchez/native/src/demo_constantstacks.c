#include <stdio.h>

#include "unwind.h"

unw_cursor_t* cursor = &(unw_cursor_t){0};
unw_context_t* context = &(unw_context_t){0};

void backtrace() {
  while (unw_step(cursor) > 0) {
    unw_word_t offset, pc;
    char sym[4096];
    if (unw_get_reg(cursor, UNW_REG_IP, &pc)) {
      printf("ERROR: cannot read program counter\n");
      exit(-1);
    }

    printf("0x%lx: ", pc);

    if (unw_get_proc_name(cursor, sym, sizeof(sym), &offset) == 0)
      printf("(%s+0x%lx)\n", sym, offset);
    else
      printf("-- no symbol name found\n");
  }
}

int cmp() {
#ifdef D_LOCAL
  procfs_PidMapPrintProc(getpid());
  backtrace();
#else
  while(1) {}
#endif
  exit(0);
}

int bar() {
  cmp();
  while(1) {}
  return 0;
}

int foo() {
  bar();
  return 0;
}

int main() {
  Map* map;
  hackptr foo_ptr = {.fun=(void(*)(void))foo};
  map = procfs_MapMatch(0, foo_ptr.num);
  printf("foo: 0x%lx, 0x%lx, 0x%lx, 0x%lx\n", foo_ptr.num, map->start, map->end, map->off);
  hackptr bar_ptr = {.fun=(void(*)(void))bar};
  map = procfs_MapMatch(0, bar_ptr.num);
  printf("bar: 0x%lx, 0x%lx, 0x%lx, 0x%lx\n", bar_ptr.num, map->start, map->end, map->off);

  // Set up libunwind
  unw_getcontext(context);
  unw_init_local(cursor, context);

  foo();
  return 0;
}
