/* Generational collector: an object that has survived a collection gets a
 * reference to a new object. The results below are required from any correct
 * collector, but the tests are aimed at the write barrier and the remembered
 * set: without them the new object is lost by the next minor collection. */
#include "common.h"

DESCRIPTOR(updatable_descriptor, 2, 0, 2, STELLA_DESCRIPTOR_UPDATABLE);

static StellaValue holder;
static StellaValue fresh;
static StellaValue scratch;

/* Leaves in `holder` a node that has survived collections. */
static void make_old_holder(uintptr_t id) {
  new_node(&holder, id, NULL, NULL);
  churn(1000);
}

static void test_only_reference_is_in_old_object(void) {
  make_old_holder(1);
  new_node(&fresh, 2, NULL, NULL);
  stella_gc_write_managed(&holder, 0, &fresh);
  fresh = NULL;
  churn(1000);

  fresh = stella_gc_read_managed(&holder, 0);
  CHECK(node_id(&fresh) == 2);
  CHECK(stella_gc_read_managed(&holder, 1) == NULL);
}

/* The new object has new children of its own. */
static void test_new_subgraph(void) {
  make_old_holder(1);
  new_node(&scratch, 3, NULL, NULL);
  new_node(&fresh, 4, NULL, NULL);
  new_node(&fresh, 2, &scratch, &fresh);
  stella_gc_write_managed(&holder, 1, &fresh);
  fresh = NULL;
  scratch = NULL;
  churn(1000);

  fresh = stella_gc_read_managed(&holder, 1);
  CHECK(node_id(&fresh) == 2);
  scratch = stella_gc_read_managed(&fresh, 0);
  CHECK(node_id(&scratch) == 3);
  scratch = stella_gc_read_managed(&fresh, 1);
  CHECK(node_id(&scratch) == 4);
}

/* Many writes to the same old object, with collections in between. */
static void test_repeated_writes(void) {
  make_old_holder(1);
  for (uintptr_t i = 0; i < 2000; ++i) {
    new_node(&fresh, i, NULL, NULL);
    stella_gc_write_managed(&holder, i % 2, &fresh);
    fresh = NULL;
    scratch = stella_gc_read_managed(&holder, i % 2);
    CHECK(node_id(&scratch) == i);
  }
  scratch = stella_gc_read_managed(&holder, 0);
  CHECK(node_id(&scratch) == 1998);
  scratch = stella_gc_read_managed(&holder, 1);
  CHECK(node_id(&scratch) == 1999);
}

/* The reference is removed before the next collection. */
static void test_reference_is_overwritten(void) {
  make_old_holder(1);
  new_node(&fresh, 2, NULL, NULL);
  stella_gc_write_managed(&holder, 0, &fresh);
  fresh = NULL;
  stella_gc_write_managed(&holder, 0, &fresh);
  churn(1000);

  CHECK(stella_gc_read_managed(&holder, 0) == NULL);
  CHECK(node_id(&holder) == 1);
}

/* A list that grows from its old end: every new node hangs on the node
 * created before it. */
static void test_list_grows_at_old_end(void) {
  enum { LENGTH = 30 };
  new_node(&holder, 0, NULL, NULL);
  scratch = holder;
  for (uintptr_t i = 1; i < LENGTH; ++i) {
    churn(50);
    new_node(&fresh, i, NULL, NULL);
    stella_gc_write_managed(&scratch, 1, &fresh);
    scratch = fresh;
    fresh = NULL;
  }
  churn(1000);

  scratch = holder;
  for (uintptr_t i = 0; i < LENGTH; ++i) {
    CHECK(node_id(&scratch) == i);
    scratch = stella_gc_read_managed(&scratch, 1);
  }
  CHECK(scratch == NULL);
}

/* stella_gc_update_object has to apply the same barrier. */
static void test_update_of_old_object(void) {
  holder = stella_gc_alloc(&updatable_descriptor);
  churn(1000);
  new_node(&fresh, 5, NULL, NULL);
  new_node(&scratch, 6, NULL, NULL);
  StellaValue *managed[] = {&fresh, &scratch};
  stella_gc_update_object(&holder, &updatable_descriptor, managed, NULL);
  fresh = NULL;
  scratch = NULL;
  churn(1000);

  fresh = stella_gc_read_managed(&holder, 0);
  CHECK(node_id(&fresh) == 5);
  fresh = stella_gc_read_managed(&holder, 1);
  CHECK(node_id(&fresh) == 6);
}

int main(void) {
  test_init(4096);
  stella_gc_push_root(&holder);
  stella_gc_push_root(&fresh);
  stella_gc_push_root(&scratch);

  test_only_reference_is_in_old_object();
  test_new_subgraph();
  test_repeated_writes();
  test_reference_is_overwritten();
  test_list_grows_at_old_end();
  test_update_of_old_object();

  stella_gc_pop_root(&scratch);
  stella_gc_pop_root(&fresh);
  stella_gc_pop_root(&holder);
  test_finish();
  return 0;
}
