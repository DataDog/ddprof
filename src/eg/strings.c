#include <signal.h>
#include <stdio.h>

#include <ddprof/string_table.h>

int main() {
  StringTable *st = stringtable_init(NULL, NULL);
  if (!st)
    return -1;

  FILE *fs = fopen("./words.txt", "r");
  char *line = NULL;
  size_t len = 0;
  ssize_t this_line, that_line;

  // Do one pass with back-to-back insert and check.  This verifies a few things
  //  * Allocation-time metadata is set properly
  //  * Allocations don't mess up incremental metadata
  //  * Referenes are propagated to nodes
  while (-1 != getline(&line, &len, fs)) {
    this_line =
        stringtable_add(st, (unsigned char *)line,
                        4); // Hardcode length since these are newline-delimited
    if (-1 == this_line) {
      printf("ADD FAILURE\n");
      raise(SIGINT);
      return -1;
    }
    that_line = stringtable_lookup(st, (unsigned char *)line, 4, NULL);
    if (that_line != this_line) {
      line[4] = 0;
      printf("LOOKUP FAILURE: %s: %ld/%ld\n", line, this_line, that_line);
      raise(SIGINT);
      return -1;
    }
  }
  fclose(fs);

  // Do one pass with just checks of the already-inserted data.  This is to
  // verify that resizing operations properly maintain metadata
  fs = fopen("./words.txt", "r");
  while (-1 != getline(&line, &len, fs)) {
    this_line = stringtable_lookup(st, (unsigned char *)line, 4, NULL);
    if (memcmp(line, (char *)stringtable_get(st, this_line), 4)) {
      line[4] = 0;
      printf("RESIZE FAILURE: %s\n", line);
      raise(SIGINT);
      stringtable_lookup(st, (unsigned char *)line, 4, NULL);
      return -1;
    }
  }
  fclose(fs);
  stringtable_free(st);
  return 0;
}
