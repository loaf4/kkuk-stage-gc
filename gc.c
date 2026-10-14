/* Generational copying collector for Stella runtime (see stella/gc.h).
 *
 * Heap consists of three spaces:
 *   young       - new objects are allocated here by bumping pointer;
 *   old         - objects that survived collection;
 *   old_reserve - empty semispace, used only by full collection.
 *
 * Minor collection copies reachable young objects to end of old and
 * empties young. Full collection copies everything reachable from young and
 * old into old_reserve, then old and old_reserve swap roles.
 *
 * Invariants:
 *   I1. Occupied memory, young plus old, never exceeds max_heap_bytes.
 *       Capacity of old and old_reserve is max_heap_bytes, so 
 *       survivors of any collection always fit into space they go to.
 *   I2. Outside of collection old_reserve is empty and no object has 
 *       forwarding pointer in its gc_word.
 *   I3. Every old object that has field pointing into young is in 
 *       remembered set. Write barrier keeps this true.
 *   I4. Static objects and NULL are outside every space, collection
 *       never moves or modifies them. */


#include <stella/gc.h>

#include "internal/gc_stats.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

typedef struct Space {
  unsigned char *start;
  unsigned char *next;
  unsigned char *end;
} Space;

/* Bits of gc_word.
 * FORWARDED:  object has been copied and rest of word is address of copy.
 *             Object sizes are 24 + 8c bytes, so object addresses are
 *             multiples of 8 and their low bits are free.
 * REMEMBERED: object is in remembered set. Only old objects get this bit
 *             and old objects are forwarded only by full collection,
 *             which overwrites whole word. */
#define GC_WORD_FORWARDED ((uintptr_t)1)
#define GC_WORD_REMEMBERED ((uintptr_t)2)

#define YOUNG_BYTES_VARIABLE "STELLA_GC_YOUNG_BYTES"

enum Lifecycle {
  LIFECYCLE_FRESH,
  LIFECYCLE_ACTIVE,
  LIFECYCLE_FINISHED
};

static enum Lifecycle lifecycle = LIFECYCLE_FRESH;
static size_t max_heap_bytes;

static Space young;
static Space old;
static Space old_reserve;

static Space *to_space;
static bool full_collection;

static StellaValue *remembered;
static size_t remembered_count;
static size_t remembered_capacity;

static StellaValue **dynamic_roots;
static size_t dynamic_root_depth;
static size_t dynamic_root_capacity;
static StellaValue **permanent_roots;
static size_t permanent_root_count;
static size_t permanent_root_capacity;

static StellaGcStats counters;

static void require_active(void) {
  if (lifecycle != LIFECYCLE_ACTIVE) {
    stella_abi_violation("collector operation outside its active lifetime");
  }
}

static bool add_overflows(size_t left, size_t right) {
  return left > SIZE_MAX - right;
}

static bool multiply_overflows(size_t left, size_t right) {
  return right != 0 && left > SIZE_MAX / right;
}

static size_t object_size(const StellaObjectDescriptor *descriptor) {
  return sizeof(StellaObject) + descriptor->slot_capacity * sizeof(StellaSlot);
}

static void validate_descriptor(const StellaObjectDescriptor *descriptor) {
  if (descriptor == NULL) {
    stella_abi_violation("null object descriptor");
  }
  if (descriptor->abi_version != STELLA_C_GC_ABI_VERSION) {
    stella_abi_violation("incompatible descriptor ABI version");
  }
  if (add_overflows(descriptor->managed_count, descriptor->primitive_count)) {
    stella_abi_violation("active field count overflows size_t");
  }
  if (descriptor->managed_count + descriptor->primitive_count >
      descriptor->slot_capacity) {
    stella_abi_violation("active fields exceed slot capacity");
  }
  if (multiply_overflows(descriptor->slot_capacity, sizeof(StellaSlot)) ||
      add_overflows(
          sizeof(StellaObject),
          descriptor->slot_capacity * sizeof(StellaSlot))) {
    stella_abi_violation("object size overflows size_t");
  }
}

static bool is_static(StellaValue object) {
  return (object->descriptor->flags & STELLA_DESCRIPTOR_STATIC) != 0;
}

static void space_create(Space *space, size_t bytes) {
  space->start = malloc(bytes);
  if (space->start == NULL) {
    stella_gc_out_of_memory(bytes);
  }
  space->next = space->start;
  space->end = space->start + bytes;
}

static size_t space_used(const Space *space) {
  return (size_t)(space->next - space->start);
}

static size_t space_free(const Space *space) {
  return (size_t)(space->end - space->next);
}

static size_t space_capacity(const Space *space) {
  return (size_t)(space->end - space->start);
}

static bool in_space(const Space *space, StellaValue object) {
  unsigned char *address = (unsigned char *)object;
  return address >= space->start && address < space->next;
}

static bool is_heap_object(StellaValue object) {
  return in_space(&young, object) || in_space(&old, object);
}

static size_t occupied_bytes(void) {
  return space_used(&young) + space_used(&old);
}

static void update_maximums(void) {
  if (space_used(&young) > counters.maximum_young_bytes) {
    counters.maximum_young_bytes = space_used(&young);
  }
  if (space_used(&old) > counters.maximum_old_bytes) {
    counters.maximum_old_bytes = space_used(&old);
  }
  if (occupied_bytes() > counters.maximum_occupied_bytes) {
    counters.maximum_occupied_bytes = occupied_bytes();
  }
}

static StellaValue require_object(StellaValue *object_root) {
  if (object_root == NULL || *object_root == NULL) {
    stella_abi_violation("null object");
  }
  StellaValue object = *object_root;
  if (!is_heap_object(object) && !is_static(object)) {
    stella_abi_violation("object is not owned by the collector");
  }
  return object;
}

static void grow_roots(
    StellaValue ***roots,
    size_t *capacity,
    size_t required) {
  if (required <= *capacity) {
    return;
  }
  if (*capacity > SIZE_MAX / 2) {
    stella_abi_violation("root stack capacity overflows size_t");
  }
  size_t next = *capacity == 0 ? 8 : *capacity * 2;
  if (next < required || multiply_overflows(next, sizeof(**roots))) {
    stella_abi_violation("root stack capacity overflows size_t");
  }
  void *storage = realloc(*roots, next * sizeof(**roots));
  if (storage == NULL) {
    stella_abi_violation("root stack exhaustion");
  }
  *roots = storage;
  *capacity = next;
}

/* Size of young generation, from STELLA_GC_YOUNG_BYTES. Default is quarter 
 * of max_heap_bytes;. Bigger value than max_heap_bytes is clamped to it. */
static size_t read_young_bytes(void) {
  size_t result = max_heap_bytes / 4;
  const char *text = getenv(YOUNG_BYTES_VARIABLE);
  if (text != NULL && *text != '\0') {
    result = 0;
    for (const char *c = text; *c != '\0'; ++c) {
      if (*c < '0' || *c > '9') {
        fprintf(
            stderr,
            "stella-gc: %s must be a positive decimal integer, got \"%s\"\n",
            YOUNG_BYTES_VARIABLE,
            text);
        exit(EXIT_FAILURE);
      }
      size_t digit = (size_t)(*c - '0');
      if (result > (SIZE_MAX - digit) / 10) {
        result = SIZE_MAX;
        break;
      }
      result = result * 10 + digit;
    }
    if (result == 0) {
      fprintf(
          stderr,
          "stella-gc: %s must be greater than zero\n",
          YOUNG_BYTES_VARIABLE);
      exit(EXIT_FAILURE);
    }
  }
  if (result > max_heap_bytes) {
    result = max_heap_bytes;
  }
  if (result == 0) {
    result = 1;
  }
  return result;
}

static bool in_from_space(StellaValue object) {
  if (in_space(&young, object)) {
    return true;
  }
  return full_collection && in_space(&old, object);
}

static bool is_forwarded(StellaValue object) {
  return (object->gc_word & GC_WORD_FORWARDED) != 0;
}

static StellaValue forwarding_address(StellaValue object) {
  return (StellaValue)(object->gc_word & ~GC_WORD_FORWARDED);
}

/* Copies object to end of to_space, then its last child that still has
 * to be copied, then that child's last such child, and so on until copied
 * object has no such children.
 * Fields of copies still hold old addresses, scan_copied fixes them later. 
 * Room in to_space is not checked: it is guaranteed by I1. */
static void chase(StellaValue object) {
  while (object != NULL) {
    const StellaObjectDescriptor *descriptor = object->descriptor;
    size_t size = object_size(descriptor);

    StellaValue copy = (StellaValue)to_space->next;
    to_space->next += size;
    memcpy(copy, object, size);
    copy->gc_word = 0;
    copy->fields = descriptor->slot_capacity == 0
        ? NULL
        : (StellaSlot *)(copy + 1);

    object->gc_word = (uintptr_t)copy | GC_WORD_FORWARDED;

    StellaValue next = NULL;
    for (size_t i = 0; i < descriptor->managed_count; ++i) {
      StellaValue child = copy->fields[i].managed;
      if (child != NULL && in_from_space(child) && !is_forwarded(child)) {
        next = child;
      }
    }
    object = next;
  }
}

/* Returns address value has after current collection. Object
 * is copied first time it is reached, later references find
 * forwarding pointer and get same copy. */
static StellaValue forward(StellaValue value) {
  if (value == NULL || !in_from_space(value)) {
    return value;
  }
  if (!is_forwarded(value)) {
    chase(value);
  }
  return forwarding_address(value);
}

static void forward_managed_fields(StellaValue object) {
  size_t managed_count = object->descriptor->managed_count;
  for (size_t i = 0; i < managed_count; ++i) {
    object->fields[i].managed = forward(object->fields[i].managed);
  }
}

static void forward_roots(void) {
  for (size_t i = 0; i < dynamic_root_depth; ++i) {
    *dynamic_roots[i] = forward(*dynamic_roots[i]);
  }
  for (size_t i = 0; i < permanent_root_count; ++i) {
    *permanent_roots[i] = forward(*permanent_roots[i]);
  }
}

/* Scanning loop of Cheney's algorithm. Objects in [scan, next) are copied,
 * but their fields still hold old addresses. Fixing field may copy more 
 * objects to end, so next grows during loop. When scan catches up with it,
 * everything reachable has been copied and fixed. Only managed fields
 * are touched: primitive fields are never pointers. */
static void scan_copied(unsigned char *scan) {
  while (scan < to_space->next) {
    StellaValue object = (StellaValue)scan;
    forward_managed_fields(object);
    scan += object_size(object->descriptor);
  }
}

static void forget_remembered(void) {
  for (size_t i = 0; i < remembered_count; ++i) {
    remembered[i]->gc_word &= ~GC_WORD_REMEMBERED;
  }
  remembered_count = 0;
}

/* Minor collection: reachable young objects are copied to end of old and
 * young becomes empty. Old objects are not traced. Roots are root stack, 
 * permanent roots and fields of remembered objects: by I3 no other old
 * object points into young. By I1 old has room for all of young. */
static void collect_minor(void) {
  if (space_used(&young) == 0) {
    return;
  }
  to_space = &old;
  full_collection = false;
  unsigned char *scan = old.next;

  forward_roots();
  for (size_t i = 0; i < remembered_count; ++i) {
    forward_managed_fields(remembered[i]);
  }
  scan_copied(scan);
  counters.minor_copied_bytes += (size_t)(old.next - scan);

  // young is empty now, no old object points to it
  forget_remembered();
  young.next = young.start;
  ++counters.minor_collections;
}

/* Full collection: everything reachable from roots, young or old, is copied 
 * into old_reserve, which then becomes old (I2 holds again afterwards).
 * This is only place where garbage in old is freed. Remembered set is
 * not root set here, and it is emptied: young is empty after collection,
 * so I3 holds with empty set. */
static void collect_full(void) {
  to_space = &old_reserve;
  full_collection = true;

  forward_roots();
  scan_copied(old_reserve.start);
  counters.full_copied_bytes += space_used(&old_reserve);

  remembered_count = 0;
  young.next = young.start;

  Space survivors = old_reserve;
  old_reserve = old;
  old = survivors;
  old_reserve.next = old_reserve.start;

  full_collection = false;
  ++counters.full_collections;
}

/* Write barrier, called before `value` is stored into field of `owner`.
 * It keeps I3: if old object gets pointer to young one, old object is added 
 * to remembered set. Whole object is remembered rather than field address, 
 * because stella_gc_update_object may later turn that field into primitive.
 * Minor collection reads fields through current descriptor. 
 * Only actual addition counts as barrier activation. */
static void write_barrier(StellaValue owner, StellaValue value) {
  if (value == NULL || !in_space(&young, value)) {
    return;
  }
  if (!in_space(&old, owner)) {
    return;
  }
  if ((owner->gc_word & GC_WORD_REMEMBERED) != 0) {
    return;
  }
  if (remembered_count == remembered_capacity) {
    size_t capacity = remembered_capacity == 0 ? 16 : remembered_capacity * 2;
    void *storage = realloc(remembered, capacity * sizeof(*remembered));
    if (storage == NULL) {
      stella_abi_violation("remembered set exhaustion");
    }
    remembered = storage;
    remembered_capacity = capacity;
  }
  remembered[remembered_count++] = owner;
  owner->gc_word |= GC_WORD_REMEMBERED;
  ++counters.write_barrier_activations;
}

void stella_gc_init(const StellaGcConfig *config) {
  if (lifecycle != LIFECYCLE_FRESH) {
    stella_abi_violation("collector initialised more than once");
  }
  if (config == NULL) {
    stella_abi_violation("null collector configuration");
  }
  if (config->abi_version != STELLA_C_GC_ABI_VERSION) {
    stella_abi_violation("incompatible collector ABI version");
  }
  if (config->max_heap_bytes == 0) {
    stella_abi_violation("zero maximum heap size");
  }
  max_heap_bytes = config->max_heap_bytes;
  space_create(&young, read_young_bytes());
  space_create(&old, max_heap_bytes);
  space_create(&old_reserve, max_heap_bytes);
  lifecycle = LIFECYCLE_ACTIVE;
}

void stella_gc_shutdown(void) {
  require_active();
  if (dynamic_root_depth != 0) {
    stella_abi_violation("runtime shutdown with dynamic roots registered");
  }
  free(young.start);
  free(old.start);
  free(old_reserve.start);
  free(remembered);
  free(dynamic_roots);
  free(permanent_roots);
  remembered = NULL;
  dynamic_roots = NULL;
  permanent_roots = NULL;
  lifecycle = LIFECYCLE_FINISHED;
}

void stella_gc_push_root(StellaValue *root) {
  require_active();
  if (root == NULL) {
    stella_abi_violation("null root slot");
  }
  grow_roots(&dynamic_roots, &dynamic_root_capacity, dynamic_root_depth + 1);
  dynamic_roots[dynamic_root_depth++] = root;
  counters.dynamic_root_depth = dynamic_root_depth;
  if (dynamic_root_depth > counters.maximum_dynamic_root_depth) {
    counters.maximum_dynamic_root_depth = dynamic_root_depth;
  }
}

void stella_gc_pop_root(StellaValue *root) {
  require_active();
  if (dynamic_root_depth == 0) {
    stella_abi_violation("dynamic root stack underflow");
  }
  if (dynamic_roots[dynamic_root_depth - 1] != root) {
    stella_abi_violation("dynamic roots popped out of order");
  }
  --dynamic_root_depth;
  counters.dynamic_root_depth = dynamic_root_depth;
}

void stella_gc_register_permanent_root(StellaValue *root) {
  require_active();
  if (root == NULL) {
    stella_abi_violation("null permanent-root slot");
  }
  grow_roots(
      &permanent_roots,
      &permanent_root_capacity,
      permanent_root_count + 1);
  permanent_roots[permanent_root_count++] = root;
  counters.permanent_root_count = permanent_root_count;
}

/* Minor collection is run when object does not fit into young 
 * or when allocating it would break I1. If I1 would still break after it,
 * full collection is run and if even then reachable objects plus new one
 * exceed max_heap_bytes, program ends with out-of-memory message.
 *
 * Object bigger than whole young generation is allocated directly in old. 
 * It always gets minor collection first: young is empty afterwards, 
 * stella_object_init_managed, which has no write barrier, cannot store 
 * young pointer into this old object and I3 holds. */
StellaValue stella_gc_alloc(const StellaObjectDescriptor *descriptor) {
  require_active();
  validate_descriptor(descriptor);
  if ((descriptor->flags & STELLA_DESCRIPTOR_STATIC) != 0) {
    stella_abi_violation("heap allocation requested with static descriptor");
  }
  size_t size = object_size(descriptor);
  if (size > max_heap_bytes) {
    ++counters.failed_allocations;
    stella_gc_out_of_memory(size);
  }

  bool large = size > space_capacity(&young);

  if (large ||
      size > space_free(&young) ||
      size > max_heap_bytes - occupied_bytes()) {
    collect_minor();
    if (size > max_heap_bytes - occupied_bytes()) {
      collect_full();
    }
    if (size > max_heap_bytes - occupied_bytes()) {
      ++counters.failed_allocations;
      stella_gc_out_of_memory(size);
    }
  }

  Space *space = large ? &old : &young;
  StellaValue object = (StellaValue)space->next;
  space->next += size;

  object->descriptor = descriptor;
  object->gc_word = 0;
  object->fields = descriptor->slot_capacity == 0
      ? NULL
      : (StellaSlot *)(object + 1);
  for (size_t i = 0; i < descriptor->managed_count; ++i) {
    object->fields[i].managed = NULL;
  }
  for (size_t i = descriptor->managed_count;
       i < descriptor->slot_capacity;
       ++i) {
    object->fields[i].primitive = 0;
  }

  counters.requested_bytes += size;
  ++counters.allocated_objects;
  if (large) {
    ++counters.old_allocations;
  }
  update_maximums();
  return object;
}

void stella_object_init_managed(
    StellaValue object,
    size_t index,
    StellaValue value) {
  require_active();
  if (object == NULL) {
    stella_abi_violation("null object");
  }
  if (!is_heap_object(object)) {
    stella_abi_violation("object is not owned by the collector");
  }
  if (index >= object->descriptor->managed_count) {
    stella_abi_violation("managed initialisation index out of range");
  }
  object->fields[index].managed = value;
}

void stella_object_init_primitive(
    StellaValue object,
    size_t index,
    StellaPrimitive value) {
  require_active();
  if (object == NULL) {
    stella_abi_violation("null object");
  }
  if (!is_heap_object(object)) {
    stella_abi_violation("object is not owned by the collector");
  }
  if (index >= object->descriptor->primitive_count) {
    stella_abi_violation("primitive initialisation index out of range");
  }
  object->fields[object->descriptor->managed_count + index].primitive = value;
}

StellaValue stella_gc_read_managed(
    StellaValue *object_root,
    size_t index) {
  require_active();
  StellaValue object = require_object(object_root);
  if (index >= object->descriptor->managed_count) {
    stella_abi_violation("managed read index out of range");
  }
  ++counters.managed_reads;
  return object->fields[index].managed;
}

void stella_gc_write_managed(
    StellaValue *object_root,
    size_t index,
    StellaValue *value_root) {
  require_active();
  StellaValue object = require_object(object_root);
  if (value_root == NULL) {
    stella_abi_violation("null value root");
  }
  if (is_static(object)) {
    stella_abi_violation("managed write to static object");
  }
  if (index >= object->descriptor->managed_count) {
    stella_abi_violation("managed write index out of range");
  }
  write_barrier(object, *value_root);
  object->fields[index].managed = *value_root;
  ++counters.managed_writes;
}

StellaPrimitive stella_object_read_primitive(
    StellaValue object,
    size_t index) {
  require_active();
  object = require_object(&object);
  if (index >= object->descriptor->primitive_count) {
    stella_abi_violation("primitive read index out of range");
  }
  ++counters.primitive_reads;
  return object->fields[object->descriptor->managed_count + index].primitive;
}

void stella_object_write_primitive(
    StellaValue object,
    size_t index,
    StellaPrimitive value) {
  require_active();
  object = require_object(&object);
  if (is_static(object)) {
    stella_abi_violation("primitive write to static object");
  }
  if (index >= object->descriptor->primitive_count) {
    stella_abi_violation("primitive write index out of range");
  }
  object->fields[object->descriptor->managed_count + index].primitive = value;
  ++counters.primitive_writes;
}

/* No collection is run here, so object stays where it is and every
 * reference to it sees update. New managed fields go through write
 * barrier (I3). Descriptor is replaced last. */
void stella_gc_update_object(
    StellaValue *object_root,
    const StellaObjectDescriptor *new_descriptor,
    StellaValue *const new_managed_roots[],
    const StellaPrimitive new_primitives[]) {
  require_active();
  StellaValue object = require_object(object_root);
  const StellaObjectDescriptor *old_descriptor = object->descriptor;
  validate_descriptor(new_descriptor);
  if ((old_descriptor->flags & STELLA_DESCRIPTOR_STATIC) != 0 ||
      (old_descriptor->flags & STELLA_DESCRIPTOR_UPDATABLE) == 0 ||
      (new_descriptor->flags & STELLA_DESCRIPTOR_STATIC) != 0) {
    stella_abi_violation("incompatible object-layout update flags");
  }
  if (old_descriptor->slot_capacity != new_descriptor->slot_capacity) {
    stella_abi_violation("object-layout update changes slot capacity");
  }
  if (new_descriptor->managed_count != 0 && new_managed_roots == NULL) {
    stella_abi_violation("missing managed values for object-layout update");
  }
  if (new_descriptor->primitive_count != 0 && new_primitives == NULL) {
    stella_abi_violation("missing primitive values for object-layout update");
  }
  for (size_t i = 0; i < new_descriptor->managed_count; ++i) {
    if (new_managed_roots[i] == NULL) {
      stella_abi_violation("null value root");
    }
  }

  for (size_t i = 0; i < new_descriptor->managed_count; ++i) {
    write_barrier(object, *new_managed_roots[i]);
    object->fields[i].managed = *new_managed_roots[i];
    ++counters.managed_writes;
  }
  for (size_t i = 0; i < new_descriptor->primitive_count; ++i) {
    size_t physical = new_descriptor->managed_count + i;
    object->fields[physical].primitive = new_primitives[i];
    ++counters.primitive_writes;
  }
  size_t active = new_descriptor->managed_count +
                  new_descriptor->primitive_count;
  for (size_t i = active; i < new_descriptor->slot_capacity; ++i) {
    object->fields[i].primitive = 0;
  }
  object->descriptor = new_descriptor;
}

void stella_gc_print_statistics(FILE *output) {
  require_active();
  if (output == NULL) {
    stella_abi_violation("null statistics output stream");
  }
  fprintf(output, "allocated bytes: %zu\n", counters.requested_bytes);
  fprintf(output, "allocated objects: %zu\n", counters.allocated_objects);
  fprintf(
      output,
      "objects allocated directly in old: %zu\n",
      counters.old_allocations);
  fprintf(output, "failed allocations: %zu\n", counters.failed_allocations);
  fprintf(
      output,
      "collections: total %zu, minor %zu, full %zu\n",
      counters.minor_collections + counters.full_collections,
      counters.minor_collections,
      counters.full_collections);
  fprintf(
      output,
      "copied bytes: minor %zu, full %zu\n",
      counters.minor_copied_bytes,
      counters.full_copied_bytes);
  fprintf(
      output,
      "occupied bytes: total %zu, young %zu, old %zu\n",
      occupied_bytes(),
      space_used(&young),
      space_used(&old));
  fprintf(
      output,
      "maximum occupied bytes: total %zu, young %zu, old %zu\n",
      counters.maximum_occupied_bytes,
      counters.maximum_young_bytes,
      counters.maximum_old_bytes);
  fprintf(output, "managed reads: %zu\n", counters.managed_reads);
  fprintf(output, "managed writes: %zu\n", counters.managed_writes);
  fprintf(output, "primitive reads: %zu\n", counters.primitive_reads);
  fprintf(output, "primitive writes: %zu\n", counters.primitive_writes);
  fprintf(output, "read barrier activations: 0 (the collector has none)\n");
  fprintf(
      output,
      "write barrier activations: %zu\n",
      counters.write_barrier_activations);
  fprintf(output, "root stack depth: %zu\n", dynamic_root_depth);
  fprintf(
      output,
      "maximum root stack depth: %zu\n",
      counters.maximum_dynamic_root_depth);
  fprintf(output, "permanent roots: %zu\n", permanent_root_count);
}

void stella_gc_print_roots(FILE *output) {
  require_active();
  if (output == NULL) {
    stella_abi_violation("null roots output stream");
  }
  for (size_t i = 0; i < dynamic_root_depth; ++i) {
    fprintf(
        output,
        "dynamic root %zu: slot=%p value=%p\n",
        i,
        (void *)dynamic_roots[i],
        (void *)*dynamic_roots[i]);
  }
  for (size_t i = 0; i < permanent_root_count; ++i) {
    fprintf(
        output,
        "permanent root %zu: slot=%p value=%p\n",
        i,
        (void *)permanent_roots[i],
        (void *)*permanent_roots[i]);
  }
}

static void print_space(FILE *output, const char *name, const Space *space) {
  fprintf(
      output,
      "%s: [%p,%p) next=%p size=%zu occupied=%zu free=%zu\n",
      name,
      (void *)space->start,
      (void *)space->end,
      (void *)space->next,
      space_capacity(space),
      space_used(space),
      space_free(space));
  unsigned char *address = space->start;
  while (address < space->next) {
    StellaValue object = (StellaValue)address;
    const StellaObjectDescriptor *descriptor = object->descriptor;
    fprintf(
        output,
        "  object=%p descriptor=%s managed=%zu primitive=%zu capacity=%zu"
        " gc_word=%" PRIuPTR "\n",
        (void *)object,
        descriptor->debug_name == NULL ? "(unnamed)" : descriptor->debug_name,
        descriptor->managed_count,
        descriptor->primitive_count,
        descriptor->slot_capacity,
        object->gc_word);
    for (size_t i = 0; i < descriptor->managed_count; ++i) {
      fprintf(
          output,
          "    managed[%zu]=%p\n",
          i,
          (void *)object->fields[i].managed);
    }
    for (size_t i = 0; i < descriptor->primitive_count; ++i) {
      fprintf(
          output,
          "    primitive[%zu]=%" PRIuPTR "\n",
          i,
          object->fields[descriptor->managed_count + i].primitive);
    }
    address += object_size(descriptor);
  }
}

void stella_gc_print_state(FILE *output) {
  require_active();
  if (output == NULL) {
    stella_abi_violation("null state output stream");
  }
  fprintf(
      output,
      "generational copying collector: occupied=%zu limit=%zu\n",
      occupied_bytes(),
      max_heap_bytes);
  print_space(output, "young", &young);
  print_space(output, "old", &old);
  print_space(output, "old reserve", &old_reserve);
  fprintf(output, "remembered objects: %zu\n", remembered_count);
  for (size_t i = 0; i < remembered_count; ++i) {
    fprintf(output, "  remembered[%zu]=%p\n", i, (void *)remembered[i]);
  }
  stella_gc_print_roots(output);
}
