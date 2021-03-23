#include <sys/types.h>
#include <unistd.h>

#include "perf.h"

int waste() {
  int a = 11;
  for(int i=0; i<100000; i++) {
    a += i;
    a = a%11;
  }
  return a;
}

void main() {
  return;
}
