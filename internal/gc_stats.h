#ifndef STELLA_GC_STATS_H
#define STELLA_GC_STATS_H
#include <stddef.h>

typedef struct StellaGcStats {
  size_t requested_bytes;
  size_t allocated_objects;
  size_t old_allocations;
  size_t failed_allocations;

  size_t minor_collections;
  size_t full_collections;
  size_t minor_copied_bytes;
  size_t full_copied_bytes;

  size_t maximum_occupied_bytes;
  size_t maximum_young_bytes;
  size_t maximum_old_bytes;

  size_t managed_reads;
  size_t managed_writes;
  size_t primitive_reads;
  size_t primitive_writes;
  size_t write_barrier_activations;

  size_t dynamic_root_depth;
  size_t maximum_dynamic_root_depth;
  size_t permanent_root_count;
} StellaGcStats;

#endif
