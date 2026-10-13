#!/usr/bin/env bash
# Builds and runs the collector tests. Run from anywhere:
#
#   tests/run.sh                  all tests, built with sanitizers
#   tests/run.sh c                only the tests written in C
#   tests/run.sh stella           only the Stella programs
#   tests/run.sh barrier          one test: a file from tests/c without ".c"
#   tests/run.sh squares          ... or a program from tests/stella/cases.txt
#   CFLAGS="-std=c11 -O2" tests/run.sh
#
# Every test is repeated for each size of the young generation in YOUNG_SIZES
# ("default" leaves STELLA_GC_YOUNG_BYTES unset).
set -u
cd "$(dirname "$0")/.."

CC=${CC:-cc}
CFLAGS=${CFLAGS:--std=c11 -Wall -Wextra -g -fsanitize=address,undefined -fno-sanitize-recover=all}
YOUNG_SIZES=${YOUNG_SIZES:-default 1 64 512 1000000}
BUILD=build/tests
ONLY=${1:-all}

# selected GROUP NAME: should the test NAME from GROUP (c or stella) run?
selected() {
  [ "$ONLY" = all ] || [ "$ONLY" = "$1" ] || [ "$ONLY" = "$2" ]
}

passed=0
failed=0
mkdir -p "$BUILD"

# run_with_young YOUNG COMMAND...
# Leaves the output in $BUILD/stdout and $BUILD/stderr, returns the exit code.
run_with_young() {
  local young=$1
  shift
  if [ "$young" = default ]; then
    env -u STELLA_GC_YOUNG_BYTES "$@" > "$BUILD/stdout" 2> "$BUILD/stderr"
  else
    env STELLA_GC_YOUNG_BYTES="$young" "$@" > "$BUILD/stdout" 2> "$BUILD/stderr"
  fi
}

pass() {
  passed=$((passed + 1))
}

fail() {
  failed=$((failed + 1))
  echo "FAIL $1"
  sed 's/^/    /' "$BUILD/stderr" | head -n 20
}

# expect_success NAME YOUNG EXPECTED_STDOUT COMMAND...
# EXPECTED_STDOUT may be "-" when the output does not matter.
expect_success() {
  local name="$1 (young=$2)" young=$2 expected=$3
  shift 3
  run_with_young "$young" "$@"
  local status=$?
  if [ $status -ne 0 ]; then
    fail "$name: exit code $status"
  elif [ "$expected" != - ] && [ "$(cat "$BUILD/stdout")" != "$expected" ]; then
    fail "$name: printed '$(cat "$BUILD/stdout")', expected '$expected'"
  else
    pass
  fi
}

# expect_message NAME YOUNG MESSAGE COMMAND...
# The program must stop by itself with a non-zero code and MESSAGE in stderr.
# A crash or a sanitizer report is a failure.
expect_message() {
  local name="$1 (young=$2)" young=$2 message=$3
  shift 3
  run_with_young "$young" "$@"
  local status=$?
  if [ $status -eq 0 ] || [ $status -ge 128 ]; then
    fail "$name: exit code $status"
  elif grep -q "Sanitizer\|runtime error" "$BUILD/stderr"; then
    fail "$name: sanitizer report"
  elif ! grep -q "$message" "$BUILD/stderr"; then
    fail "$name: no '$message' in stderr"
  else
    pass
  fi
}

build_c_test() {
  $CC $CFLAGS -Iinclude "tests/c/$1.c" runtime/runtime.c gc.c -o "$BUILD/$1"
}

build_stella_program() {
  $CC $CFLAGS -Iinclude "$1.c" \
    runtime/runtime.c runtime/scalar.c runtime/main.c gc.c \
    -o "$BUILD/$(basename "$1")"
}

for source in tests/c/*.c; do
  name=$(basename "$source" .c)
  selected c "$name" || continue
  echo "== $name"
  if ! build_c_test "$name"; then
    failed=$((failed + 1))
    echo "FAIL $name: does not build"
    continue
  fi
  for young in $YOUNG_SIZES; do
    case $name in
      violations)
        for case_name in $("$BUILD/violations"); do
          expect_message "violations $case_name" "$young" "ABI violation" \
            "$BUILD/violations" "$case_name"
        done
        ;;
      oom)
        for case_name in $("$BUILD/oom"); do
          expect_message "oom $case_name" "$young" "cannot allocate" \
            "$BUILD/oom" "$case_name"
        done
        # Filling the heap exactly to the limit has to succeed first.
        run_with_young "$young" "$BUILD/oom" exact_fill
        if [ "$(cat "$BUILD/stdout")" = filled ]; then
          pass
        else
          fail "oom exact_fill (young=$young): the heap was not filled"
        fi
        ;;
      random)
        for seed in 1 2 3; do
          expect_success "random $seed" "$young" - "$BUILD/random" "$seed"
        done
        ;;
      *)
        expect_success "$name" "$young" - "$BUILD/$name"
        ;;
    esac
  done
done

# Lines of cases.txt: PROGRAM ARGUMENT LIMIT EXPECTED, where PROGRAM is a path
# without the extension and LIMIT is in bytes or "default".
if [ -f tests/stella/cases.txt ]; then
  built=""
  while read -r program argument limit expected; do
    case $program in
      "" | "#"*) continue ;;
    esac
    name=$(basename "$program")
    selected stella "$name" || continue
    case " $built " in
      *" $name "*) ;;
      *)
        echo "== $name"
        if ! build_stella_program "$program"; then
          failed=$((failed + 1))
          echo "FAIL $name: does not build"
          continue
        fi
        built="$built $name"
        ;;
    esac
    for young in $YOUNG_SIZES; do
      if [ "$limit" = default ]; then
        expect_success "$name $argument" "$young" "$expected" \
          "$BUILD/$name" "$argument"
      else
        expect_success "$name $argument limit=$limit" "$young" "$expected" \
          "$BUILD/$name" "$argument" "$limit"
      fi
    done
  done < tests/stella/cases.txt
fi

if [ $((passed + failed)) -eq 0 ]; then
  echo "no test is named '$ONLY'"
  exit 1
fi
echo "passed: $passed, failed: $failed"
[ "$failed" -eq 0 ]
