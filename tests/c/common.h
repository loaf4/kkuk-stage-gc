#ifndef STELLA_GC_TESTS_COMMON_H
#define STELLA_GC_TESTS_COMMON_H

#include <stella/gc.h>

#include <stdio.h>
#include <stdlib.h>

#define CHECK(condition) \
  do { \
    if (!(condition)) { \
      fprintf( \
          stderr, \
          "%s:%d: check failed: %s\n", \
          __FILE__, \
          __LINE__, \
          #condition); \
      exit(2); \
    } \
  } while (0)

#define DESCRIPTOR(name, managed, primitive, capacity, descriptor_flags) \
  static const StellaObjectDescriptor name = { \
      .abi_version = STELLA_C_GC_ABI_VERSION, \
      .kind = STELLA_OBJECT_DATA, \
      .flags = (descriptor_flags), \
      .entry_arity = 0, \
      .managed_count = (managed), \
      .primitive_count = (primitive), \
      .slot_capacity = (capacity), \
      .entry_code = NULL, \
      .debug_name = #name}

/* A node has two children and a numeric id: 48 bytes. */
DESCRIPTOR(node_descriptor, 2, 1, 3, 0);
/* 32 bytes. */
DESCRIPTOR(garbage_descriptor, 1, 0, 1, 0);

static inline void test_init(size_t max_heap_bytes) {
  StellaGcConfig config = {STELLA_C_GC_ABI_VERSION, max_heap_bytes};
  stella_gc_init(&config);
}

static inline void test_finish(void) {
  if (getenv("TEST_GC_STATS") != NULL) {
    stella_gc_print_statistics(stderr);
  }
  stella_gc_shutdown();
}

/* Allocates `count` objects that immediately become garbage. When
 * count * 32 bytes is several times the heap limit, collections must happen
 * whatever the size of the young generation is. */
static inline void churn(size_t count) {
  StellaValue garbage = NULL;
  stella_gc_push_root(&garbage);
  for (size_t i = 0; i < count; ++i) {
    garbage = stella_gc_alloc(&garbage_descriptor);
  }
  stella_gc_pop_root(&garbage);
}

/* The child roots may be NULL pointers. result_root may be one of them. */
static inline void new_node(
    StellaValue *result_root,
    uintptr_t id,
    StellaValue *left_root,
    StellaValue *right_root) {
  StellaValue node = stella_gc_alloc(&node_descriptor);
  stella_object_init_managed(node, 0, left_root == NULL ? NULL : *left_root);
  stella_object_init_managed(node, 1, right_root == NULL ? NULL : *right_root);
  stella_object_init_primitive(node, 0, id);
  *result_root = node;
}

static inline uintptr_t node_id(StellaValue *node_root) {
  return stella_object_read_primitive(*node_root, 0);
}

#endif
