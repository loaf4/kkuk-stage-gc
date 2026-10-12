/* General interface obligation: when the reachable objects and the requested
 * one do not fit in the limit, the program ends with an out-of-memory
 * message. Every case here must end that way.
 *
 * Usage: oom CASE. Without arguments prints the case names. */
#include "common.h"

#include <string.h>

DESCRIPTOR(huge_descriptor, 0, 0, 1000, 0);

/* Fills the heap to the last byte with reachable objects (this must work),
 * prints "filled" and asks for one more object. */
static void case_exact_fill(void) {
  enum { LIMIT = 4096, OBJECT_BYTES = 32 };
  test_init(LIMIT);
  StellaValue head = NULL;
  StellaValue fresh = NULL;
  stella_gc_push_root(&head);
  stella_gc_push_root(&fresh);
  for (int i = 0; i < LIMIT / OBJECT_BYTES; ++i) {
    fresh = stella_gc_alloc(&garbage_descriptor);
    stella_object_init_managed(fresh, 0, head);
    head = fresh;
  }
  puts("filled");
  fflush(stdout);
  fresh = stella_gc_alloc(&garbage_descriptor);
}

/* Garbage does not help when the reachable part alone is too big. */
static void case_live_data_grows(void) {
  test_init(4096);
  StellaValue head = NULL;
  stella_gc_push_root(&head);
  for (uintptr_t i = 0; i < 1000; ++i) {
    churn(10);
    new_node(&head, i, NULL, &head);
  }
}

static void case_object_larger_than_limit(void) {
  test_init(1024);
  StellaValue object = NULL;
  stella_gc_push_root(&object);
  object = stella_gc_alloc(&huge_descriptor);
}

int main(int argc, char *argv[]) {
  if (argc != 2) {
    puts("exact_fill live_data_grows object_larger_than_limit");
    return 0;
  }
  if (strcmp(argv[1], "exact_fill") == 0) {
    case_exact_fill();
  } else if (strcmp(argv[1], "live_data_grows") == 0) {
    case_live_data_grows();
  } else if (strcmp(argv[1], "object_larger_than_limit") == 0) {
    case_object_larger_than_limit();
  } else {
    fprintf(stderr, "unknown case %s\n", argv[1]);
    return 3;
  }
  fputs("out of memory was not reported\n", stderr);
  return 3;
}
