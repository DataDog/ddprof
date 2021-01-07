#ifndef  _H_ddog
#define _H_ddog

#include <stdlib.h>
#include <ctype.h>

#include "pprof.h"
#include "http.h"

#ifdef DD_DBG_PROFGEN
#  include <fcntl.h>
#  include <sys/stat.h>
#  include <sys/types.h>
#endif

#define MIN(x,y) ({__typeof__ (x) _x = (x); __typeof__ (y) _y = (y); _x < _y ? _x : _y;})

typedef struct DDRequest {
  char*  host;
  char*  port;
  char*  key;
  char*  env;
  char*  site;
  char*  service;
  char*  version;
  Dict* D;
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

#define MCP(x) if (x) free(x); strdup(buf)
#define STR(x) #x
#define GEV(x,y) if (!ddr->x && (buf = getenv(STR(y)))) MCP(ddr->x)
void DDRequestInit(DDRequest* ddr) {
  // TODO what to do about the dict?
  char* buf = NULL;
  GEV(host, DD_AGENT_HOST);
  GEV(port, DD_TRACE_AGENT_PORT);
  GEV(key, DD_API_KEY);
  GEV(site, DD_SITE);
  GEV(env, DD_ENV);
  GEV(service, DD_SERVICE);
  GEV(version, DD_VERSION);

  if(ddr->key && !apikey_isvalid(ddr->key))
    memset(ddr->key, 0, sizeof(ddr->key));

  return;
}
#undef MCP
#undef STR
#undef GEV

void DDRequestSend(DDRequest* ddr, DProf* dp) {
  PPProfile* pprof = &dp->pprof;
  printf("SENDIT\n");
  // Update pprof duration
  pprof_durationUpdate(dp);

  // Serialize and zip pprof
  char* buf;
  size_t sz_packed = perftools__profiles__profile__get_packed_size(pprof);
  size_t sz_zipped = pprof_zip(dp, (buf=malloc(sz_packed)), sz_packed);

  // Add API key if one is provided
  if(strlen(ddr->key))
    DictSet(ddr->D, "DD_API_KEY", ddr->key, strlen(ddr->key)+1);

  // Add zipped payload to dictionary
  DictSet(ddr->D, "pprof[0]", buf, sz_zipped); // TODO DO NOT COPY THIS.

#ifdef DD_DBG_PROFGEN
  printf("Printing a pprof!\n");
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
  pprof_sampleClear(dp);
  pprof_timeUpdate(dp);
}


#endif
