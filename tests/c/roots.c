/* General interface obligations: permanent roots, a deep root stack, roots
 * that hold NULL or get a new value after registration. */
#include "common.h"

static StellaValue permanent;
static StellaValue permanent_empty;

static void test_permanent_root(void) {
  stella_gc_register_permanent_root(&permanent);
  stella_gc_register_permanent_root(&permanent_empty);

  new_node(&permanent, 11, NULL, NULL);
  churn(2000);
  CHECK(node_id(&permanent) == 11);
  CHECK(permanent_empty == NULL);

  new_node(&permanent, 12, &permanent, NULL);
  churn(2000);
  CHECK(node_id(&permanent) == 12);
  permanent_empty = stella_gc_read_managed(&permanent, 0);
  CHECK(node_id(&permanent_empty) == 11);
  permanent_empty = NULL;
}

static void test_deep_root_stack(void) {
  enum { DEPTH = 20000, STEP = 100 };
  StellaValue *slots = calloc(DEPTH, sizeof(*slots));
  CHECK(slots != NULL);

  for (size_t i = 0; i < DEPTH; ++i) {
    stella_gc_push_root(&slots[i]);
  }
  for (size_t i = 0; i < DEPTH; i += STEP) {
    new_node(&slots[i], i, NULL, NULL);
  }
  churn(3000);
  for (size_t i = 0; i < DEPTH; ++i) {
    if (i % STEP == 0) {
      CHECK(node_id(&slots[i]) == i);
    } else {
      CHECK(slots[i] == NULL);
    }
  }
  for (size_t i = DEPTH; i > 0; --i) {
    stella_gc_pop_root(&slots[i - 1]);
  }
  free(slots);
}

int main(void) {
  test_init(32768);
  test_permanent_root();
  test_deep_root_stack();
  CHECK(node_id(&permanent) == 12);
  test_finish();
  return 0;
}
