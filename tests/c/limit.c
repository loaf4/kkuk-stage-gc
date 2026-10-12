/* General interface obligation: a program whose reachable objects together
 * with the requested one fit in half of the limit runs to the end, however
 * much it allocates in total. */
#include "common.h"

int main(void) {
  enum { LIMIT = 2048, LENGTH = 20 };
  test_init(LIMIT);
  StellaValue head = NULL;
  StellaValue cursor = NULL;
  stella_gc_push_root(&head);
  stella_gc_push_root(&cursor);

  /* 20 nodes are 960 bytes; with one more request that is 1008 <= 1024. */
  for (uintptr_t i = 0; i < LENGTH; ++i) {
    new_node(&head, i, NULL, &head);
  }
  /* 100 rounds of 32 KiB of garbage each, 1500 times the limit in total. */
  for (int round = 0; round < 100; ++round) {
    churn(1000);
    cursor = head;
    for (uintptr_t i = LENGTH; i > 0; --i) {
      CHECK(node_id(&cursor) == i - 1);
      cursor = stella_gc_read_managed(&cursor, 1);
    }
    CHECK(cursor == NULL);
  }

  stella_gc_pop_root(&cursor);
  stella_gc_pop_root(&head);
  test_finish();
  return 0;
}
