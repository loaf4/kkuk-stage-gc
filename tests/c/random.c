/* Random operations on a graph of nodes, compared with a model of the same
 * graph kept in ordinary C arrays. The model knows the id of every root and
 * the ids of both children of every node.
 *
 * Usage: random [SEED] */
#include "common.h"

enum {
  ROOTS = 16,
  STEPS = 20000,
  /* All roots are cleared every RESET_EVERY steps, so at most 200 nodes
   * (9600 bytes) are reachable: less than half of the limit. */
  RESET_EVERY = 200,
  CHECK_EVERY = 50,
  LIMIT = 32768,
  NONE = -1
};

static StellaValue roots[ROOTS];
static StellaValue scratch;

static int root_node[ROOTS];
static int children[2][STEPS];
static int node_count;

static unsigned seed = 1;

static unsigned next_random(unsigned bound) {
  seed = seed * 1103515245u + 12345u;
  return (seed >> 16) % bound;
}

static void check_value(StellaValue *value_root, int expected) {
  if (expected == NONE) {
    CHECK(*value_root == NULL);
  } else {
    CHECK(*value_root != NULL);
    CHECK(node_id(value_root) == (uintptr_t)expected);
  }
}

static void check_against_model(void) {
  for (int i = 0; i < ROOTS; ++i) {
    check_value(&roots[i], root_node[i]);
    if (root_node[i] == NONE) {
      continue;
    }
    for (int field = 0; field < 2; ++field) {
      scratch = stella_gc_read_managed(&roots[i], (size_t)field);
      check_value(&scratch, children[field][root_node[i]]);
    }
  }
  scratch = NULL;
}

static void clear_roots(void) {
  for (int i = 0; i < ROOTS; ++i) {
    roots[i] = NULL;
    root_node[i] = NONE;
  }
}

static void random_step(void) {
  unsigned a = next_random(ROOTS);
  unsigned b = next_random(ROOTS);
  unsigned target = next_random(ROOTS);
  unsigned field = next_random(2);

  switch (next_random(6)) {
    case 0:
    case 1:
    case 2: {
      int id = node_count++;
      children[0][id] = root_node[a];
      children[1][id] = root_node[b];
      new_node(&roots[target], (uintptr_t)id, &roots[a], &roots[b]);
      root_node[target] = id;
      break;
    }
    case 3:
      if (root_node[a] != NONE) {
        stella_gc_write_managed(&roots[a], field, &roots[b]);
        children[field][root_node[a]] = root_node[b];
      }
      break;
    case 4:
      if (root_node[a] != NONE) {
        roots[target] = stella_gc_read_managed(&roots[a], field);
        root_node[target] = children[field][root_node[a]];
      }
      break;
    default:
      roots[a] = NULL;
      root_node[a] = NONE;
      break;
  }
}

int main(int argc, char *argv[]) {
  if (argc == 2) {
    seed = (unsigned)strtoul(argv[1], NULL, 10);
  }
  test_init(LIMIT);
  stella_gc_push_root(&scratch);
  for (int i = 0; i < ROOTS; ++i) {
    stella_gc_push_root(&roots[i]);
  }
  clear_roots();

  for (int step = 1; step <= STEPS; ++step) {
    random_step();
    if (step % CHECK_EVERY == 0) {
      check_against_model();
    }
    if (step % RESET_EVERY == 0) {
      clear_roots();
    }
  }

  for (int i = ROOTS; i > 0; --i) {
    stella_gc_pop_root(&roots[i - 1]);
  }
  stella_gc_pop_root(&scratch);
  test_finish();
  return 0;
}
