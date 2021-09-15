#pragma once

typedef struct DDProfContext DDProfContext;

typedef struct WorkerAttr {
  DDRes (*init_fun)(DDProfContext *);
  DDRes (*finish_fun)(DDProfContext *);
} WorkerAttr;
