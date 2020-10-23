#ifndef  _H_ddog
#define _H_ddog

#include <stdlib.h>
#include <ctype.h>

#include "pprof.h"
#include "http.h"

// Avoid double-eval
#define MIN(x,y) ({__typeof__ (x) _x = (x); __typeof__ (y) _y = (y); _x < _y ? _x : _y;})

typedef struct DDRequest {
  char  host[256];
  char  port[6];       // max 65536+0
  char  key[33];
  char  env[64];
  char  site[128];
  char  service[128];
  char  version[128];
  Dict* D;             // tags, etc
} DDRequest;

void ddr_addtag(DDRequest* ddr, char* tag, char* val) {
  DictSet(ddr->D, tag, val, 1+strlen(val));
}

char apikey_isvalid(char* key) {
  size_t n = 0;
  if(!key)                      return 0;
  if(32 != ((n=strlen(key)))) return 0;

  for(size_t i=0; i<n; i++)
    if(!islower(key[i]) && !isdigit(key[i]))
      return 0;
  return 1;
}

void DDRequestInit(DDRequest* ddr) {
  // TODO what to do about the dict?
#define MCP(x) memcpy((x), buf, MIN(sizeof(x), strlen(buf)))
  char* buf = NULL;
  if((buf = getenv("DD_AGENT_HOST")))
    MCP(ddr->host);
  if((buf = getenv("DD_TRACE_AGENT_PORT")))
    MCP(ddr->port);
  if((buf = getenv("DD_API_KEY")))
    MCP(ddr->key);
  if((buf = getenv("DD_SITE")))
    MCP(ddr->site);
  if((buf = getenv("DD_ENV")))
    MCP(ddr->env);
  if((buf = getenv("DD_SERVICE")))
    MCP(ddr->service);
  if((buf = getenv("DD_VERSION")))
    MCP(ddr->service);

  if(ddr->key && !apikey_isvalid(ddr->key))
    memset(ddr->key, 0, sizeof(ddr->key));

  return;
#undef MCP
}

#ifdef DD_DBG_PROFGEN
#  include <sys/types.h>
#  include <sys/stat.h>
#  include <fcntl.h>
#endif

void DDRequestSend(DDRequest* ddr, PPProfile* pprof) {
  printf("SENDIT\n");
  // Update pprof duration
  pprof_durationUpdate(pprof);

  // Serialize and zip pprof
  char* buf;
  size_t sz_packed = perftools__profiles__profile__get_packed_size(pprof);
  size_t sz_zipped = pprof_zip(pprof, (buf=malloc(sz_packed)), sz_packed);

  // Add API key if one is provided
  if(strlen(ddr->key))
    DictSet(ddr->D, "DD_API_KEY", ddr->key, strlen(ddr->key)+1);

  // Add zipped payload to dictionary
  DictSet(ddr->D, "pprof[0]", buf, sz_zipped); // TODO DO NOT COPY THIS.

#ifdef DD_DBG_PROFGEN
  mkdir("./pprofs", 0777);
  unlink("./pprofs/native.pb.gz");
  int fd = open("./pprofs/native.pb.gz", O_RDWR | O_CREAT, 0677);
  write(fd, buf, sz_zipped);
  close(fd);
#endif

  // Send
  if(HttpSendMultipart(ddr->host, ddr->port, "/v1/input", ddr->D)) {
    printf("<error> some kind of problem\n");
    free(buf);
    return;
  }

  // Cleanup
  DictSet(ddr->D, "pprof[0]", "", sizeof(char));
  free(buf);
  pprof_sampleClear(pprof);
  pprof_timeUpdate(pprof);
}


#endif
