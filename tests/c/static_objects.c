/* General interface obligation: static objects are neither moved nor
 * modified, in roots and in fields alike. */
#include "common.h"

DESCRIPTOR(static_leaf_descriptor, 0, 0, 0, STELLA_DESCRIPTOR_STATIC);
DESCRIPTOR(static_box_descriptor, 1, 1, 2, STELLA_DESCRIPTOR_STATIC);

static StellaObject static_leaf = {&static_leaf_descriptor, 777, NULL};
static StellaSlot static_box_slots[2];
static StellaObject static_box = {&static_box_descriptor, 0, static_box_slots};

int main(void) {
  static_box_slots[0].managed = &static_leaf;
  static_box_slots[1].primitive = 99;

  test_init(4096);
  StellaValue leaf = &static_leaf;
  StellaValue box = &static_box;
  StellaValue holder = NULL;
  stella_gc_push_root(&leaf);
  stella_gc_push_root(&box);
  stella_gc_push_root(&holder);

  new_node(&holder, 1, &leaf, &box);
  churn(1000);

  CHECK(leaf == &static_leaf);
  CHECK(box == &static_box);
  CHECK(stella_gc_read_managed(&holder, 0) == &static_leaf);
  CHECK(stella_gc_read_managed(&holder, 1) == &static_box);
  CHECK(static_leaf.descriptor == &static_leaf_descriptor);
  CHECK(static_leaf.gc_word == 777);
  CHECK(static_box.gc_word == 0);
  CHECK(static_box.fields == static_box_slots);
  CHECK(stella_gc_read_managed(&box, 0) == &static_leaf);
  CHECK(stella_object_read_primitive(box, 0) == 99);

  stella_gc_pop_root(&holder);
  stella_gc_pop_root(&box);
  stella_gc_pop_root(&leaf);
  test_finish();
  return 0;
}
