#include <stdio.h>
#include <stdlib.h>

void *__tls_get_addr(void *v) {
  fprintf(stderr, "__tls_get_addr was called, which is not allowed!\n");
  abort();
}
