#pragma once

#include "ddres_def.h"

struct DsoHdr;

DDRes libdso_init(struct DsoHdr **dso_hdr);
void libdso_free(struct DsoHdr *dso_hdr);
