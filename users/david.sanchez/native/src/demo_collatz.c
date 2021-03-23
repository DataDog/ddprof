#include <stdlib.h>
#include <stdio.h>

#include <unistd.h>

int collatz(int n) {
  return (n<=1) ? 0 : collatz((n%2) ? 3*n+1 : n/2);
}

int main(int argc, char** argv) {
  int max = 10;
  if(1 < argc)
    max = atoi(argv[1]);
  int n = 1;
  while(1) {
//    if(fork())
//      return 0;
    for(int i=1; i<max; i++) collatz(i);
    for(int j=0; j<10*n; j++)  { __asm__ volatile("pause"); usleep(1); }
  }
}
