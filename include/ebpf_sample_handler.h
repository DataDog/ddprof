#pragma once

#include <stddef.h>

/* Receive events from the ring buffer. */
int sample_handler(void *_ctx, void *data, size_t size);
