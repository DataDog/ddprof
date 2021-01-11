#include <stdio.h>

#include "string_table.h"

int main() {
  StringTable* st = stringtable_init(NULL);
  if(!st) return -1;

  FILE* fs = fopen("./words.txt", "r");
  char* line = NULL; size_t len = 0;
  ssize_t n;
  size_t lines=0;
  while(-1 != (n=getline(&line, &len, fs))) {
    line[4] = 0;
    if(-1 == stringtable_add(st, (unsigned char*)line, 4)) { // hardcoded length
      printf("FAILURE\n");
      return -1;
    }
    lines++;
  }
  fclose(fs);

  // And return everything
  printf("Processed %ld lines\n", lines);
  stringtable_free(st);
  return 0;
}
