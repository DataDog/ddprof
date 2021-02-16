#ifndef _H_http
#define _H_http

#include <ctype.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "dictionary.h"

char *RandomNameMake(char *s, int n) {
  static char tokens[] = "0123456789abcdef";
  s[n] = 0;
  for (int i = 0; i < n; i++)
    s[i] = tokens[rand() % (sizeof(tokens) - 1)];
  return s;
}

#define AS_CHUNK 4096
typedef struct AppendString {
  char *str;
  size_t sz;
  size_t n; // Always points to the start of the next allocation
} AppendString;

AppendString *ASInit(AppendString *as) {
  as->str = calloc(AS_CHUNK, sizeof(char));
  return as;
}

void ASGrow(AppendString *as, size_t addtl) {
  size_t sz = as->n + addtl;
  if (as->sz < sz) {
    size_t chunks = 1 + (sz + 1) / AS_CHUNK;
    char *buf = calloc(chunks * AS_CHUNK, sizeof(char));
    memcpy(buf, as->str, as->n);
    free(as->str);
    as->str = buf;
  }
}

// Doesn't assume that str is null-terminated, since it may actually be binary
// Conventionally this would be an unsigned char, but this is temporary
void ASAdd(AppendString *as, char *str, size_t sz) {
  ASGrow(as, sz + 1);
  memcpy(&as->str[as->n], str, sz);
  as->n += sz;
}

void ASStrAdd(AppendString *as, char *str) { ASAdd(as, str, strlen(str)); }

void ASIntAdd(AppendString *as, size_t i) {
  char buf[32] = {0};
  snprintf(buf, 32, "%ld", i);
  ASStrAdd(as, buf);
}

/******************************************************************************\
|*                                Socket Stuff                                *|
\******************************************************************************/
typedef struct MultiItem {
  char *disposition;
  char *type;
  char *body;
  size_t sz_body;
} MultiItem;

void ASAddMulti(AppendString *as, char *boundary, MultiItem *mi) {
  if (!mi)
    return;
  ASStrAdd(as, boundary);
  ASStrAdd(as, "\r\n");
  if (mi->disposition) {
    ASStrAdd(as, "Content-Disposition: form-data; name=\"");
    ASStrAdd(as, mi->disposition);
    ASStrAdd(as, "\"\r\n");
  }
  if (mi->type) {
    ASStrAdd(as, "Content-Type: ");
    ASStrAdd(as, mi->type);
    ASStrAdd(as, "\"\r\n");
  }
  ASStrAdd(as, "\r\n");
  ASAdd(as, mi->body, mi->sz_body ? mi->sz_body : strlen(mi->body));
  ASStrAdd(as, "\r\n");
}

typedef enum HTTP_RET {
  HTTP_OK = 0,
  HTTP_EADDR,
  HTTP_ESOCK,
  HTTP_ECONN,
} HTTP_RET;

// string_table copypasta, replace with functions
#define STR_LEN_PTR(x) ((uint32_t *)&(x)[-4])
#define STR_LEN(x) (*STR_LEN_PTR(x))

#define DG(x) dictionary_get_cstr(payload, (x))
#define MISUB(x, y) ASAddMulti(&as_bod, boundary, &(MultiItem){x, NULL, (y)})
#define MISUBD(x, y)                                                           \
  ASAddMulti(&as_bod, boundary, &(MultiItem){x, NULL, (char *)DG(y)})
char HttpSendMultipart(const char *host, const char *port, const char *route,
                       Dictionary *payload) {
  struct addrinfo *addr;
  if (getaddrinfo(host, port, NULL, &addr)) {
    return HTTP_EADDR;
  }

  // Connect
  int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
  if (-1 == fd) {
    return HTTP_ESOCK;
  }
  if (connect(fd, addr->ai_addr, addr->ai_addrlen)) {
    return HTTP_ECONN;
  }

  // Get some parameters together
  char boundary[60 + 4];
  RandomNameMake(boundary, 62);
  boundary[0] = boundary[1] = '-';
  static char hdr_host[256] = {0};
  snprintf(hdr_host, 256, "Host: %s:%s\r\n", host, port);
  static char hdr_agent[] =
      "User-Agent: Native-http-client/0.1\r\n"; // TODO version header
  static char hdr_accept[] = "Accept: */*\r\n";
  static char hdr_apikey[256];
  static char hdr_content[256] = {0};
  snprintf(hdr_content, 256,
           "Content-Type: multipart/form-data; boundary=%s\r\n", &boundary[2]);
  static char hdr_encoding[] = "Accept-Encoding: gzip\r\n";

  // If an API key is defined, use it
  unsigned char *val;
  if ((val = DG("DD_API_KEY")))
    snprintf(hdr_apikey, 255, "DD-API-KEY:%s\r\n", (char *)val);

  // Put together the payload and compute the length
  AppendString as_hdr = {0};
  ASInit(&as_hdr);
  AppendString as_bod = {0};
  ASInit(&as_bod);

  // Put together the time strings
  char time_start[128] = {0};
  char time_end[128] = {0};
  time_t now;
  time(&now);
  now -= 60;
  struct tm *now_tm = localtime(&now);
  strftime(time_start, 128, "%Y-%m-%dT%H:%M:%SZ", now_tm);
  now += 60;
  now_tm = localtime(&now);
  strftime(time_end, 128, "%Y-%m-%dT%H:%M:%SZ", now_tm);

  // Populate payload
  MISUB("recording-start", time_start);
  MISUB("recording-end", time_end);
  MISUBD("tags[]", "tags.host");
  MISUBD("tags[]", "tags.service");
  MISUBD("tags[]", "tags.language");
  if ((val = DG("pprof[0]"))) {
    size_t val_len = *(size_t *)DG("pprof[0].length");
    ASAddMulti(&as_bod, boundary,
               &(MultiItem){"data[0]\"; filename=\"pprof-data",
                            "application/octet-stream", (char *)val, val_len});
  }
  MISUB("types[0]", "samples,cpu"); // TODO Don't hardcode
  MISUB("format", "pprof");
  MISUBD("tags[]", "tags.runtime");
  MISUBD("runtime", "runtime");
  MISUBD("tags[]", "tags.prof_ver");
  MISUBD("tags[]", "tags.os");

  // Populate headers
  char header0[1024] = {0};
  snprintf(header0, 1024, "POST %s HTTP/1.1\r\n", route);
  ASStrAdd(&as_hdr, header0);
  ASStrAdd(&as_hdr, hdr_host);
  ASStrAdd(&as_hdr, hdr_agent);
  ASStrAdd(&as_hdr, hdr_accept);
  ASStrAdd(&as_hdr, hdr_apikey);
  ASStrAdd(&as_hdr, hdr_content);
  ASStrAdd(&as_hdr, hdr_encoding);
  ASStrAdd(&as_hdr, "Content-Length: ");
  ASIntAdd(&as_hdr, as_bod.n);
  ASStrAdd(&as_hdr, "\r\n");

  // Send it over!
  send(fd, as_hdr.str, as_hdr.n, 0);
  send(fd, "\r\n\r\n", 4, 0);
  send(fd, as_bod.str, as_bod.n, 0);

  close(fd);
  return HTTP_OK;
}
#undef MISUB
#undef MISUBD

#endif
