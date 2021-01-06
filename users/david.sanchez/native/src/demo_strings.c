#include "string_table.h"

int main() {
  StringTable* st = stringtable_init();
  if(!st) return -1;
  st->logging = 1; // enable logging to file

  char w1[] = "";
  char w2[] = "HELLO!";
  char w3[] = "espresso";
  char w4[] = "generatingfunctionology";

  stringtable_add_cstr(st, w1);
  stringtable_add_cstr(st, w2);
  stringtable_add_cstr(st, w3);
  stringtable_add_cstr(st, w4);

  // Did it work?
  printf("Index: %ld\n", stringtable_lookup_cstr(st, w1));
  printf("Word: %s\n", stringtable_get(st, 1));

  printf("Index: %ld\n", stringtable_lookup_cstr(st, w4));
  printf("Word: %s\n", stringtable_get(st, 3));

  // And return everything
  stringtable_free(st);
  return 0;
}
