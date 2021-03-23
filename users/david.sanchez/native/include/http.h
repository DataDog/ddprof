#include <stdlib.h>
#include <ctype.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

typedef struct fat {
  void*  ptr;
  size_t sz;
} fat;

// Done dirt cheap
// TODO fine for now, but do a dictionary correctly
#define DICT_SIZE 128
typedef struct Dict {
  size_t n;
  void* key[DICT_SIZE];
  fat*  val[DICT_SIZE];
} Dict;

// This is cute until malloc fails.
void* fatdup(const void* A, const size_t sz_A) {
  return memcpy(malloc(sz_A), A, sz_A);
}

int64_t DictSet(Dict* D, const char* k, const void* v, const size_t sz_v) {
  if(D->n > DICT_SIZE); // TODO
  if(!k || !v || !sz_v)
    return -1;
  for(int i=0; i<D->n; i++)
    if(!strcmp(k, D->key[i]))
      return i;
  D->key[D->n] = strdup(k);
  D->val[D->n] = memcpy(malloc(sz_v+sizeof(size_t)), v, sz_v);
  D->val[D->n]->sz = sz_v;
  return D->n++;
}

void* DictGet(Dict* D, const char* k) {
  for(int i=0; i<D->n; i++)
    if(!strcmp(k, D->key[i]))
      return D->val[i];
  return NULL;
}

void DictClear(Dict* D) {
  for(int i=0; i<D->n; i++) {
    if(D->key[i]) free(D->key[i]);
    if(D->val[i]) free(D->val[i]);
  }
}

char* RandomNameMake(char* s, int n) {
  static char tokens[] = "0123456789abcdef";
  s[n] = 0;
  for(int i=0; i<n; i++)
    s[i] = tokens[rand()%(sizeof(tokens)-1)];
  return s;
}

#define AS_CHUNK 4096
typedef struct AppendString {
  char* str;
  size_t sz;
  size_t n;  // Always points to the start of the next allocation
} AppendString;

AppendString* ASInit(AppendString* as) {
  as->str = calloc(AS_CHUNK, sizeof(char));
  return as;
}

void ASGrow(AppendString* as, size_t addtl) {
  size_t sz = as->n + addtl;
  if(as->sz < sz) {
    size_t chunks = 1+(sz+1)/AS_CHUNK;
    char* buf = calloc(chunks*AS_CHUNK, sizeof(char));
    memcpy(buf, as->str, as->n);
    free(as->str);
    as->str = buf;
  }
}

// Doesn't assume that str is null-terminated, since it may actually be binary
// Conventionally this would be an unsigned char, but this is temporary
void ASAdd(AppendString* as, char* str, size_t sz) {
  ASGrow(as, sz);
  memcpy(&as->str[as->n], str, sz);
  as->n += sz;
}

void ASStrAdd(AppendString* as, char* str) {
  ASAdd(as, str, strlen(str));
}

void ASIntAdd(AppendString* as, size_t i) {
  char buf[32] = {0};
  snprintf(buf, 32, "%ld", i);
  ASStrAdd(as, buf);
}

/******************************************************************************\
|*                                Socket Stuff                                *|
\******************************************************************************/
typedef struct MultiItem {
  char* disposition;
  char* type;
  char* body;
  size_t sz_body;
} MultiItem;

void ASAddMulti(AppendString* as, char* boundary, MultiItem* mi) {
  if(!mi) return;
  ASStrAdd(as, boundary); ASStrAdd(as, "\r\n");
  if(mi->disposition) {
    ASStrAdd(as, "Content-Disposition: form-data; name=\"");
    ASStrAdd(as, mi->disposition);
    ASStrAdd(as, "\"\r\n");
  }
  if(mi->type) {
    ASStrAdd(as, "Content-Type: ");
    ASStrAdd(as, mi->type);
    ASStrAdd(as, "\"\r\n");
  }
  ASStrAdd(as, "\r\n");
  ASAdd(as, mi->body, mi->sz_body ? mi->sz_body : strlen(mi->body));
  ASStrAdd(as, "\r\n");
}

void HttpSendMultipart(const char* host, const int port, const char* route, Dict* payload) {
  // Do the lookup
  // TODO strip out protocol, etc, from host
  struct addrinfo* addr;
  char sport[25] = {0};
  snprintf(sport, 24, "%d", port);
  if(getaddrinfo(host, sport, NULL, &addr)) ; // TODO err

  // Connect
  int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
  if(connect(fd, addr->ai_addr, addr->ai_addrlen)) ; // TODO err

  // Get some parameters together
  char boundary[60+4];
  RandomNameMake(boundary, 62);
  boundary[0] = boundary[1] = '-';
  char hdr_host[256] = {0}; snprintf(hdr_host, 256, "Host: %s:%d\r\n", host, port);
  char hdr_agent[] = "User-Agent: Native-http-client/0.1\r\n";
  char hdr_content[256] = {0}; snprintf(hdr_content, 256, "Content-Type: multipart/form-data; boundary=%s\r\n", &boundary[2]);
  char hdr_encoding[] = "Accept-Encoding: gzip\r\n";

  // Put together the payload and compute the length
  AppendString as_hdr = {0}; ASInit(&as_hdr);
  AppendString as_bod = {0}; ASInit(&as_bod);

  // Populate payload
  ASAddMulti(&as_bod, boundary, &(MultiItem){"format", NULL, "pprof"});
  ASAddMulti(&as_bod, boundary, &(MultiItem){"tags[]", NULL, "host:localhost(lol)"});
  ASAddMulti(&as_bod, boundary, &(MultiItem){"tags[]", NULL, "runtime:native"});
  ASAddMulti(&as_bod, boundary, &(MultiItem){"tags[]", NULL, "profiler-version:v0.1"});
  ASAddMulti(&as_bod, boundary, &(MultiItem){"tags[]", NULL, "runtime-os:Linux"});
  ASAddMulti(&as_bod, boundary, &(MultiItem){"types[0]", NULL, "samples,cpu"});
  if(DictGet(payload, "pprof[0]")) {
    fat* packed_pprof = (fat*)DictGet(payload, "pprof[0]");
    ASAddMulti(&as_bod, boundary, &(MultiItem){"data[0]\"; filename=\"pprof-data", "application/octet-stream", (char*)packed_pprof->ptr, packed_pprof->sz });
  }

  // Populate headers
  ASStrAdd(&as_hdr, "POST /profiling/v1/input HTTP/1.1\r\n");
  ASStrAdd(&as_hdr, hdr_host);
  ASStrAdd(&as_hdr, hdr_agent);
  ASStrAdd(&as_hdr, hdr_content);
  ASStrAdd(&as_hdr, hdr_encoding);
  ASStrAdd(&as_hdr, "Content-Length: "); ASIntAdd(&as_hdr, as_bod.n); ASStrAdd(&as_hdr, "\r\n");

  // Send it over!
  send(fd, as_hdr.str, as_hdr.n, 0);
  send(fd, "\r\n\r\n", 4, 0);
  send(fd, as_bod.str, as_bod.n, 0);
}
