/* General interface obligations: the shape of the reachable graph survives
 * collections (shared children, cycles, several roots for one object). */
#include "common.h"

static void test_shared_child(void) {
  StellaValue shared = NULL;
  StellaValue left = NULL;
  StellaValue right = NULL;
  stella_gc_push_root(&shared);
  stella_gc_push_root(&left);
  stella_gc_push_root(&right);

  new_node(&shared, 7, NULL, NULL);
  new_node(&left, 1, &shared, NULL);
  new_node(&right, 2, &shared, NULL);
  shared = NULL;
  churn(1000);

  shared = stella_gc_read_managed(&left, 0);
  CHECK(shared != NULL);
  CHECK(shared == stella_gc_read_managed(&right, 0));
  CHECK(node_id(&shared) == 7);
  CHECK(node_id(&left) == 1);
  CHECK(node_id(&right) == 2);

  stella_gc_pop_root(&right);
  stella_gc_pop_root(&left);
  stella_gc_pop_root(&shared);
}

static void test_cycle_of_two(void) {
  StellaValue first = NULL;
  StellaValue second = NULL;
  stella_gc_push_root(&first);
  stella_gc_push_root(&second);

  new_node(&first, 1, NULL, NULL);
  new_node(&second, 2, &first, NULL);
  stella_gc_write_managed(&first, 0, &second);
  second = NULL;
  churn(1000);

  second = stella_gc_read_managed(&first, 0);
  CHECK(node_id(&second) == 2);
  CHECK(stella_gc_read_managed(&second, 0) == first);
  CHECK(node_id(&first) == 1);

  stella_gc_pop_root(&second);
  stella_gc_pop_root(&first);
}

static void test_self_reference(void) {
  StellaValue self = NULL;
  stella_gc_push_root(&self);

  new_node(&self, 3, NULL, NULL);
  stella_gc_write_managed(&self, 0, &self);
  stella_gc_write_managed(&self, 1, &self);
  churn(1000);

  CHECK(stella_gc_read_managed(&self, 0) == self);
  CHECK(stella_gc_read_managed(&self, 1) == self);
  CHECK(node_id(&self) == 3);

  stella_gc_pop_root(&self);
}

static void test_several_roots_for_one_object(void) {
  StellaValue first = NULL;
  StellaValue second = NULL;
  StellaValue third = NULL;
  stella_gc_push_root(&first);
  stella_gc_push_root(&second);
  stella_gc_push_root(&third);

  new_node(&first, 4, NULL, NULL);
  second = first;
  third = first;
  churn(1000);

  CHECK(first == second);
  CHECK(second == third);
  CHECK(node_id(&third) == 4);

  stella_gc_pop_root(&third);
  stella_gc_pop_root(&second);
  stella_gc_pop_root(&first);
}

/* A long chain: every node is the last child of the previous one. */
static void test_list_keeps_order(void) {
  enum { LENGTH = 40 };
  StellaValue head = NULL;
  StellaValue cursor = NULL;
  stella_gc_push_root(&head);
  stella_gc_push_root(&cursor);

  for (uintptr_t i = 0; i < LENGTH; ++i) {
    new_node(&head, i, NULL, &head);
  }
  churn(1000);

  cursor = head;
  for (uintptr_t i = LENGTH; i > 0; --i) {
    CHECK(cursor != NULL);
    CHECK(node_id(&cursor) == i - 1);
    CHECK(stella_gc_read_managed(&cursor, 0) == NULL);
    cursor = stella_gc_read_managed(&cursor, 1);
  }
  CHECK(cursor == NULL);

  stella_gc_pop_root(&cursor);
  stella_gc_pop_root(&head);
}

/* Unreachable cycles must be freed: 2000 of them are 48 times the limit. */
static void test_garbage_cycles_are_freed(void) {
  StellaValue first = NULL;
  StellaValue second = NULL;
  stella_gc_push_root(&first);
  stella_gc_push_root(&second);

  for (uintptr_t i = 0; i < 2000; ++i) {
    new_node(&first, i, NULL, NULL);
    new_node(&second, i, &first, NULL);
    stella_gc_write_managed(&first, 0, &second);
  }

  stella_gc_pop_root(&second);
  stella_gc_pop_root(&first);
}

int main(void) {
  test_init(4096);
  test_shared_child();
  test_cycle_of_two();
  test_self_reference();
  test_several_roots_for_one_object();
  test_list_keeps_order();
  test_garbage_cycles_are_freed();
  test_finish();
  return 0;
}
