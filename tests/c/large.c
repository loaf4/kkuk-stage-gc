/* Generational collector: an object that does not fit into the young
 * generation. With STELLA_GC_YOUNG_BYTES below 344 the wide object is
 * allocated in the old generation while its future fields are still young;
 * with a bigger young generation this is an ordinary test of a wide object. */
#include "common.h"

enum { WIDTH = 40 };

DESCRIPTOR(wide_descriptor, WIDTH, 0, WIDTH, 0);

static StellaValue wide;
static StellaValue children[WIDTH];
static StellaValue scratch;

static void check_children(uintptr_t first_id) {
  for (size_t i = 0; i < WIDTH; ++i) {
    scratch = stella_gc_read_managed(&wide, i);
    CHECK(node_id(&scratch) == first_id + i);
  }
}

int main(void) {
  test_init(16384);
  stella_gc_push_root(&wide);
  stella_gc_push_root(&scratch);
  for (size_t i = 0; i < WIDTH; ++i) {
    stella_gc_push_root(&children[i]);
  }

  /* The fields are computed first and the object is allocated after them,
   * like in generated code. The initialisation has no write barrier. */
  for (size_t i = 0; i < WIDTH; ++i) {
    new_node(&children[i], 100 + i, NULL, NULL);
  }
  wide = stella_gc_alloc(&wide_descriptor);
  for (size_t i = 0; i < WIDTH; ++i) {
    stella_object_init_managed(wide, i, children[i]);
    children[i] = NULL;
  }
  churn(2000);
  check_children(100);

  /* Ordinary writes of new objects into the same wide object. */
  for (size_t i = 0; i < WIDTH; ++i) {
    new_node(&scratch, 200 + i, NULL, NULL);
    stella_gc_write_managed(&wide, i, &scratch);
    scratch = NULL;
    churn(20);
  }
  churn(2000);
  check_children(200);

  /* Several wide objects in a row, only the last one stays reachable. */
  for (int round = 0; round < 200; ++round) {
    new_node(&scratch, 300, NULL, NULL);
    wide = stella_gc_alloc(&wide_descriptor);
    for (size_t i = 0; i < WIDTH; ++i) {
      stella_object_init_managed(wide, i, scratch);
    }
  }
  churn(2000);
  scratch = stella_gc_read_managed(&wide, 0);
  CHECK(node_id(&scratch) == 300);
  CHECK(stella_gc_read_managed(&wide, WIDTH - 1) == scratch);

  for (size_t i = WIDTH; i > 0; --i) {
    stella_gc_pop_root(&children[i - 1]);
  }
  stella_gc_pop_root(&scratch);
  stella_gc_pop_root(&wide);
  test_finish();
  return 0;
}
