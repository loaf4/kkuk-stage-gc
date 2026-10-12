/* General interface obligation: every interface violation from section 4.8
 * ends with a message from stella_abi_violation, not with a crash.
 *
 * Usage: violations CASE. Without arguments prints the case names. */
#include "common.h"

#include <string.h>

DESCRIPTOR(updatable_descriptor, 1, 1, 2, STELLA_DESCRIPTOR_UPDATABLE);
DESCRIPTOR(same_capacity_descriptor, 1, 1, 2, 0);
DESCRIPTOR(other_capacity_descriptor, 1, 1, 3, 0);
DESCRIPTOR(static_descriptor, 1, 1, 2, STELLA_DESCRIPTOR_STATIC);
DESCRIPTOR(node_shaped_descriptor, 2, 1, 3, 0);

DESCRIPTOR(too_many_fields_descriptor, 2, 2, 3, 0);
DESCRIPTOR(field_count_overflow_descriptor, SIZE_MAX, 1, SIZE_MAX, 0);
DESCRIPTOR(size_overflow_multiply_descriptor, 0, 0, SIZE_MAX, 0);
DESCRIPTOR(size_overflow_add_descriptor, 0, 0, SIZE_MAX / sizeof(StellaSlot), 0);

static const StellaObjectDescriptor bad_version_descriptor = {
    .abi_version = 2,
    .kind = STELLA_OBJECT_DATA,
    .managed_count = 1,
    .slot_capacity = 1};

static StellaSlot static_slots[2];
static StellaObject static_object = {&static_descriptor, 0, static_slots};

static StellaValue root;
static StellaValue other_root;

static void start(void) {
  test_init(4096);
  stella_gc_push_root(&root);
  stella_gc_push_root(&other_root);
}

static void start_with_node(void) {
  start();
  new_node(&root, 1, NULL, NULL);
}

static void start_with_updatable(void) {
  start();
  root = stella_gc_alloc(&updatable_descriptor);
}

static void update_root(const StellaObjectDescriptor *new_descriptor) {
  StellaValue *managed[] = {&other_root, &other_root};
  StellaPrimitive primitives[] = {0, 0};
  stella_gc_update_object(&root, new_descriptor, managed, primitives);
}

/* Lifecycle. */

static void alloc_before_init(void) {
  stella_gc_alloc(&node_descriptor);
}

static void push_root_before_init(void) {
  stella_gc_push_root(&root);
}

static void shutdown_before_init(void) {
  stella_gc_shutdown();
}

static void init_twice(void) {
  test_init(4096);
  test_init(4096);
}

static void alloc_after_shutdown(void) {
  test_init(4096);
  stella_gc_shutdown();
  stella_gc_alloc(&node_descriptor);
}

static void push_root_after_shutdown(void) {
  test_init(4096);
  stella_gc_shutdown();
  stella_gc_push_root(&root);
}

static void shutdown_with_roots(void) {
  start();
  stella_gc_shutdown();
}

/* Descriptors passed to stella_gc_alloc. */

static void alloc_null_descriptor(void) {
  start();
  stella_gc_alloc(NULL);
}

static void alloc_bad_version(void) {
  start();
  stella_gc_alloc(&bad_version_descriptor);
}

static void alloc_static_descriptor(void) {
  start();
  stella_gc_alloc(&static_descriptor);
}

static void alloc_too_many_fields(void) {
  start();
  stella_gc_alloc(&too_many_fields_descriptor);
}

static void alloc_field_count_overflow(void) {
  start();
  stella_gc_alloc(&field_count_overflow_descriptor);
}

static void alloc_size_overflow_multiply(void) {
  start();
  stella_gc_alloc(&size_overflow_multiply_descriptor);
}

static void alloc_size_overflow_add(void) {
  start();
  stella_gc_alloc(&size_overflow_add_descriptor);
}

/* Roots. */

static void pop_not_the_top_root(void) {
  start();
  stella_gc_pop_root(&root);
}

static void pop_from_empty_stack(void) {
  test_init(4096);
  stella_gc_pop_root(&root);
}

/* Field access with NULL instead of an object. */

static void read_managed_null_object(void) {
  start();
  stella_gc_read_managed(&root, 0);
}

static void write_managed_null_object(void) {
  start();
  stella_gc_write_managed(&root, 0, &other_root);
}

static void read_primitive_null_object(void) {
  start();
  stella_object_read_primitive(NULL, 0);
}

static void write_primitive_null_object(void) {
  start();
  stella_object_write_primitive(NULL, 0, 1);
}

static void init_managed_null_object(void) {
  start();
  stella_object_init_managed(NULL, 0, NULL);
}

static void init_primitive_null_object(void) {
  start();
  stella_object_init_primitive(NULL, 0, 1);
}

/* Field access with an index outside of its area. A node has two managed
 * fields and one primitive field. */

static void read_managed_bad_index(void) {
  start_with_node();
  stella_gc_read_managed(&root, 2);
}

static void write_managed_bad_index(void) {
  start_with_node();
  stella_gc_write_managed(&root, 2, &other_root);
}

static void read_primitive_bad_index(void) {
  start_with_node();
  stella_object_read_primitive(root, 1);
}

static void write_primitive_bad_index(void) {
  start_with_node();
  stella_object_write_primitive(root, 1, 1);
}

static void init_managed_bad_index(void) {
  start();
  root = stella_gc_alloc(&node_descriptor);
  stella_object_init_managed(root, 2, NULL);
}

static void init_primitive_bad_index(void) {
  start();
  root = stella_gc_alloc(&node_descriptor);
  stella_object_init_primitive(root, 1, 1);
}

/* Writes to a static object. */

static void write_managed_to_static(void) {
  start();
  root = &static_object;
  stella_gc_write_managed(&root, 0, &other_root);
}

static void write_primitive_to_static(void) {
  start();
  stella_object_write_primitive(&static_object, 0, 1);
}

/* stella_gc_update_object. */

static void update_null_object(void) {
  start();
  update_root(&same_capacity_descriptor);
}

static void update_not_updatable(void) {
  start_with_node();
  update_root(&node_shaped_descriptor);
}

static void update_to_static_descriptor(void) {
  start_with_updatable();
  update_root(&static_descriptor);
}

static void update_changes_capacity(void) {
  start_with_updatable();
  update_root(&other_capacity_descriptor);
}

typedef struct Case {
  const char *name;
  void (*run)(void);
} Case;

#define CASE(function) {#function, function}

static const Case cases[] = {
    CASE(alloc_before_init),
    CASE(push_root_before_init),
    CASE(shutdown_before_init),
    CASE(init_twice),
    CASE(alloc_after_shutdown),
    CASE(push_root_after_shutdown),
    CASE(shutdown_with_roots),
    CASE(alloc_null_descriptor),
    CASE(alloc_bad_version),
    CASE(alloc_static_descriptor),
    CASE(alloc_too_many_fields),
    CASE(alloc_field_count_overflow),
    CASE(alloc_size_overflow_multiply),
    CASE(alloc_size_overflow_add),
    CASE(pop_not_the_top_root),
    CASE(pop_from_empty_stack),
    CASE(read_managed_null_object),
    CASE(write_managed_null_object),
    CASE(read_primitive_null_object),
    CASE(write_primitive_null_object),
    CASE(init_managed_null_object),
    CASE(init_primitive_null_object),
    CASE(read_managed_bad_index),
    CASE(write_managed_bad_index),
    CASE(read_primitive_bad_index),
    CASE(write_primitive_bad_index),
    CASE(init_managed_bad_index),
    CASE(init_primitive_bad_index),
    CASE(write_managed_to_static),
    CASE(write_primitive_to_static),
    CASE(update_null_object),
    CASE(update_not_updatable),
    CASE(update_to_static_descriptor),
    CASE(update_changes_capacity),
};

int main(int argc, char *argv[]) {
  size_t case_count = sizeof(cases) / sizeof(cases[0]);
  if (argc != 2) {
    for (size_t i = 0; i < case_count; ++i) {
      puts(cases[i].name);
    }
    return 0;
  }
  for (size_t i = 0; i < case_count; ++i) {
    if (strcmp(argv[1], cases[i].name) == 0) {
      cases[i].run();
      fputs("the violation was not detected\n", stderr);
      return 3;
    }
  }
  fprintf(stderr, "unknown case %s\n", argv[1]);
  return 3;
}
