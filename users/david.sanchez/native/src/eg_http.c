#include "http.h"

void main() {
  Dict d = {0};
  Dict d2 = {0};
  DictSet(&d, "sub", (void*)&d2, sizeof(d2));
  DictSet((Dict*)DictGet(&d, "sub"), "Hello", "MEOW", 1+strlen("MEOW"));
//  printf("%s\n",(char*)DictGet(&d2,"Hello"));   // NOT THIS
  printf("%s\n", (char*)DictGet( DictGet(&d, "sub"), "Hello")); // THIS

  // Try some appendstring stuff
  AppendString as = {0};
  ASInit(&as);
//  for(int i=0; i<4095; i++) ASStrAdd(&as, (char[]){97+(i%26)}); ASStrAdd(&as, "\n");
//  for(int i=0; i<4096; i++) ASStrAdd(&as, (char[]){97+(i%26)}); ASStrAdd(&as, "\n");
//  for(int i=0; i<4097; i++) ASStrAdd(&as, (char[]){97+(i%26)}); ASStrAdd(&as, "\n");
//  printf("%s\n", as.str);
  HttpSendMultipart("localhost", 5555, "", NULL);
}

