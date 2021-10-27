#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct ProducerLinearizer {
  uint64_t sz;        // number of allocated slots
  uint64_t *A;        // Array of values of length sz; allocated by interface
  uint64_t *I;        // Array of indices; allocated by interface
  bool *F;            // Mask of free indices; allocated by interface
  uint64_t freecount; // count of the slots in F set to true
  uint64_t cursor;    // see pop documentation
} ProducerLinearizer;

// Initializes a ProducerLinearizer object, allocates storage
bool ProducerLinearizer_init(ProducerLinearizer *pl, uint64_t sz);

// Frees the storage under a ProducerLinearizer
void ProducerLinearizer_free(ProducerLinearizer *pl);

// Pushes an item to the ProducerLinearizer.  This is for the purposes of
// updating the free list
bool ProducerLinearizer_push(ProducerLinearizer *pl, uint64_t i, uint64_t v);

// Gets the current index of the top item, setting the underlying slot to free.
// Returns false if there are no available items
bool ProducerLinearizer_pop(ProducerLinearizer *pl, uint64_t *ret);
