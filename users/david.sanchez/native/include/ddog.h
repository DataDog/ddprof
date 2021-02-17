#ifndef _H_ddog
#define _H_ddog

#include <ctype.h>
#include <stdlib.h>

#include "dd_send.h"
#include "pprof.h"

#ifdef DD_DBG_PROFGEN
#  include <fcntl.h>
#  include <sys/stat.h>
#  include <sys/types.h>
#endif

void DDRequestSend(DDRequest *ddr, DProf *dp) {
  // Add API key if one is provided
  if (strlen(ddr->key))
    dictionary_put_cstr(ddr->D, "DD_API_KEY", ddr->key, strlen(ddr->key));

  // Add zipped payload to dictionary.  This stashes the pointer to the buffer,
  // so note that it still needs to be freed!
  size_t sz = 0;
  unsigned char *buf = pprof_flush(dp, &sz);
  dictionary_put_cstr(ddr->D, "pprof[0]", buf, sizeof(unsigned char *));
  dictionary_put_cstr(ddr->D, "pprof[0].length", (void *)&sz, sizeof(size_t));

  // Send
  if (HttpSendMultipart(ddr->host, ddr->port, "/v1/input", ddr->D)) {
    printf("<error> some kind of problem\n");
    free(buf);
    return;
  }

  // Cleanup
  dictionary_put_cstr(ddr->D, "pprof[0]", "", sizeof(char));
  free(buf);
  pprof_timeUpdate(dp);
}

#endif
