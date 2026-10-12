/* General interface obligations: primitive fields are kept bit for bit,
 * inactive fields are zero, a new object starts zeroed. */
#include "common.h"

DESCRIPTOR(mixed_descriptor, 1, 2, 5, 0);
DESCRIPTOR(empty_descriptor, 0, 0, 0, 0);

static void test_primitive_that_looks_like_a_pointer(void) {
  StellaValue target = NULL;
  StellaValue object = NULL;
  stella_gc_push_root(&target);
  stella_gc_push_root(&object);

  new_node(&target, 5, NULL, NULL);
  object = stella_gc_alloc(&mixed_descriptor);
  stella_object_init_managed(object, 0, target);
  stella_object_init_primitive(object, 0, (uintptr_t)target);
  stella_object_init_primitive(object, 1, UINTPTR_MAX);
  uintptr_t address_as_number = (uintptr_t)target;
  target = NULL;
  churn(1000);

  /* The managed field follows the object, the primitive one must not. */
  CHECK(stella_object_read_primitive(object, 0) == address_as_number);
  CHECK(stella_object_read_primitive(object, 1) == UINTPTR_MAX);
  target = stella_gc_read_managed(&object, 0);
  CHECK(node_id(&target) == 5);

  CHECK(object->descriptor == &mixed_descriptor);
  CHECK(object->fields == (StellaSlot *)(object + 1));
  CHECK(object->fields[3].primitive == 0);
  CHECK(object->fields[4].primitive == 0);

  stella_object_write_primitive(object, 1, 123);
  churn(1000);
  CHECK(stella_object_read_primitive(object, 0) == address_as_number);
  CHECK(stella_object_read_primitive(object, 1) == 123);

  stella_gc_pop_root(&object);
  stella_gc_pop_root(&target);
}

static void test_new_object_is_zeroed(void) {
  StellaValue object = NULL;
  stella_gc_push_root(&object);

  /* Fill the memory with non-zero words first. */
  for (int i = 0; i < 500; ++i) {
    new_node(&object, UINTPTR_MAX, NULL, NULL);
    stella_gc_write_managed(&object, 0, &object);
    stella_gc_write_managed(&object, 1, &object);
  }
  for (int i = 0; i < 100; ++i) {
    object = stella_gc_alloc(&mixed_descriptor);
    CHECK(stella_gc_read_managed(&object, 0) == NULL);
    CHECK(stella_object_read_primitive(object, 0) == 0);
    CHECK(stella_object_read_primitive(object, 1) == 0);
    CHECK(object->fields[3].primitive == 0);
    CHECK(object->fields[4].primitive == 0);
  }

  stella_gc_pop_root(&object);
}

static void test_objects_without_fields(void) {
  StellaValue first = NULL;
  StellaValue second = NULL;
  stella_gc_push_root(&first);
  stella_gc_push_root(&second);

  first = stella_gc_alloc(&empty_descriptor);
  second = stella_gc_alloc(&empty_descriptor);
  churn(1000);

  CHECK(first != NULL && second != NULL);
  CHECK(first != second);
  CHECK(first->descriptor == &empty_descriptor);
  CHECK(second->descriptor == &empty_descriptor);

  stella_gc_pop_root(&second);
  stella_gc_pop_root(&first);
}

int main(void) {
  test_init(4096);
  test_primitive_that_looks_like_a_pointer();
  test_new_object_is_zeroed();
  test_objects_without_fields();
  test_finish();
  return 0;
}
