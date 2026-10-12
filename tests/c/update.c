/* General interface obligations for stella_gc_update_object: the object keeps
 * its identity, m and p may change, inactive fields become zero. */
#include "common.h"

DESCRIPTOR(pair_updatable, 2, 0, 3, STELLA_DESCRIPTOR_UPDATABLE);
DESCRIPTOR(mixed_final, 1, 1, 3, 0);
DESCRIPTOR(words_updatable, 0, 3, 3, STELLA_DESCRIPTOR_UPDATABLE);
DESCRIPTOR(triple_updatable, 3, 0, 3, STELLA_DESCRIPTOR_UPDATABLE);
DESCRIPTOR(empty_final, 0, 0, 3, 0);

static void test_identity_is_kept(void) {
  StellaValue object = NULL;
  StellaValue alias = NULL;
  StellaValue holder = NULL;
  StellaValue first = NULL;
  StellaValue second = NULL;
  StellaValue third = NULL;
  stella_gc_push_root(&object);
  stella_gc_push_root(&alias);
  stella_gc_push_root(&holder);
  stella_gc_push_root(&first);
  stella_gc_push_root(&second);
  stella_gc_push_root(&third);

  new_node(&first, 1, NULL, NULL);
  new_node(&second, 2, NULL, NULL);
  new_node(&third, 3, NULL, NULL);
  object = stella_gc_alloc(&pair_updatable);
  stella_object_init_managed(object, 0, first);
  stella_object_init_managed(object, 1, second);
  alias = object;
  new_node(&holder, 9, &object, NULL);

  StellaValue *managed[] = {&third};
  StellaPrimitive primitives[] = {42};
  stella_gc_update_object(&object, &mixed_final, managed, primitives);

  for (int round = 0; round < 2; ++round) {
    CHECK(alias == object);
    CHECK(stella_gc_read_managed(&holder, 0) == object);
    CHECK(object->descriptor == &mixed_final);
    CHECK(stella_gc_read_managed(&object, 0) == third);
    CHECK(stella_object_read_primitive(object, 0) == 42);
    CHECK(object->fields[2].primitive == 0);
    CHECK(node_id(&third) == 3);
    churn(1000);
  }

  stella_gc_pop_root(&third);
  stella_gc_pop_root(&second);
  stella_gc_pop_root(&first);
  stella_gc_pop_root(&holder);
  stella_gc_pop_root(&alias);
  stella_gc_pop_root(&object);
}

static void test_field_counts_change(void) {
  StellaValue object = NULL;
  StellaValue first = NULL;
  StellaValue second = NULL;
  StellaValue third = NULL;
  stella_gc_push_root(&object);
  stella_gc_push_root(&first);
  stella_gc_push_root(&second);
  stella_gc_push_root(&third);

  new_node(&first, 1, NULL, NULL);
  new_node(&second, 2, NULL, NULL);
  object = stella_gc_alloc(&pair_updatable);
  stella_object_init_managed(object, 0, first);
  stella_object_init_managed(object, 1, second);

  /* m: 2 -> 0. The former pointers stay as plain numbers and must be neither
   * followed nor updated when the nodes die. */
  StellaPrimitive words[] = {(uintptr_t)first, (uintptr_t)second, 5};
  stella_gc_update_object(&object, &words_updatable, NULL, words);
  first = NULL;
  second = NULL;
  churn(1000);
  CHECK(object->descriptor == &words_updatable);
  CHECK(stella_object_read_primitive(object, 0) == words[0]);
  CHECK(stella_object_read_primitive(object, 1) == words[1]);
  CHECK(stella_object_read_primitive(object, 2) == 5);

  /* m: 0 -> 3. The object has survived collections, the values are new. */
  new_node(&first, 10, NULL, NULL);
  new_node(&second, 20, NULL, NULL);
  new_node(&third, 30, NULL, NULL);
  StellaValue *managed[] = {&first, &second, &third};
  stella_gc_update_object(&object, &triple_updatable, managed, NULL);
  first = NULL;
  second = NULL;
  third = NULL;
  churn(1000);
  first = stella_gc_read_managed(&object, 0);
  second = stella_gc_read_managed(&object, 1);
  third = stella_gc_read_managed(&object, 2);
  CHECK(node_id(&first) == 10);
  CHECK(node_id(&second) == 20);
  CHECK(node_id(&third) == 30);

  /* m: 3 -> 0, p: 0 -> 0. Both arrays may be NULL. */
  stella_gc_update_object(&object, &empty_final, NULL, NULL);
  CHECK(object->descriptor == &empty_final);
  CHECK(object->fields[0].primitive == 0);
  CHECK(object->fields[1].primitive == 0);
  CHECK(object->fields[2].primitive == 0);
  churn(1000);
  CHECK(object->descriptor == &empty_final);
  CHECK(object->fields[0].primitive == 0);

  stella_gc_pop_root(&third);
  stella_gc_pop_root(&second);
  stella_gc_pop_root(&first);
  stella_gc_pop_root(&object);
}

int main(void) {
  test_init(4096);
  test_identity_is_kept();
  test_field_counts_change();
  test_finish();
  return 0;
}
