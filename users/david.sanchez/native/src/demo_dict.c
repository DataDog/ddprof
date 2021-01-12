#include "dictionary.h"

int main() {
  Dict* dict = dict_init();
  if(!dict) return -1;

  FILE* fs = fopen("./words.txt", "r");
  char* line = NULL; size_t len = 0;
  ssize_t n;
  while(-1 != (n=getline(&line, &len, fs))) {
    if(-1 == dict_add(dict, (unsigned char*)line, len, (unsigned char*)line, len)) {
      printf("FAILURE\n");
      return -1;
    }
  }
  fclose(fs);

  // Validate
  fs = fopen("./words.txt", "r");
  n=0;
  while(-1 != (n=getline(&line, &len, fs))) {
    unsigned char* val = dict_get(dict, (unsigned char*)line, len-1);
    if(!val)
      continue;
    if(memcmp(val, line, len-1)) {
      printf("[FAIL] %s != %s\n", dict_get(dict, (unsigned char*)line, len-1), line);
    }
  }
  return 0;
}
