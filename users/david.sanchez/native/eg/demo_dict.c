#include <stdio.h>
#include <stdlib.h>

#include "dictionary.h"

int main() {
  Dictionary *dict = dictionary_init(NULL, NULL);
  if (!dict)
    return -1; // error
  char key1[] = "HELLO";
  dictionary_put_cstr(dict, key1, "5\0", 2);

  printf("String: %s\n", (char *)dictionary_get_cstr(dict, key1));

  dictionary_del_cstr(dict, key1);

  if (DICT_NA == dictionary_get_cstr(dict, key1))
    printf((DICT_NA == dictionary_get_cstr(dict, key1)) ? "SUCCESS\n"
                                                        : "FAILURE\n");

  dictionary_add_cstr(dict, key1, "1234\0", 5);
  printf("String: %s\n", (char *)dictionary_get_cstr(dict, key1));

  if (-1 == dictionary_add_cstr(dict, key1, "1234\0", 5))
    printf("SUCCESS\n");

  dictionary_put_cstr(dict, key1, "4321\0", 5);
  printf("String: %s\n", (char *)dictionary_get_cstr(dict, key1));

  // Cool!  Now try stuffing it
  FILE *fs = fopen("./words.txt", "r");
  char *line = NULL;
  size_t len = 0;
  size_t lines = 0;
  while (-1 != getline(&line, &len, fs)) {
    line[4] = 0;
    if (-1 == dictionary_add_cstr(dict, line, "foo\0", 4)) {
      printf("FAILURE\n");
      return -1;
    }
    lines++;
  }
  fclose(fs);

  // Now do another scan and make sure after resizing that all entries can be
  // obtained
  fs = fopen("./words.txt", "r");
  while (-1 != getline(&line, &len, fs)) {
    line[4] = 0;
    if (!dictionary_get_cstr(dict, line)) {
      printf("FAILURE\n");
      return -1;
    }
    lines++;
  }
  fclose(fs);

  dictionary_free(dict);
  free(dict);
  return 0;
}
