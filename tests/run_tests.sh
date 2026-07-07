#!/usr/bin/env bash
# Integration tests: drive the interactive shell end-to-end through stdin
# and assert on its output and exit codes. Each scenario runs in a scratch
# directory so database.dat never leaks between tests (or into the repo).
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
BIN="$ROOT/bin/database"
pass=0
fail=0

new_scratch() {
  SCRATCH=$(mktemp -d)
  cd "$SCRATCH"
}

run() { # feeds each argument as one stdin line; sets OUT and LAST_STATUS
  OUT=$(printf '%s\n' "$@" | "$BIN" 2>&1)
  LAST_STATUS=$?
}

expect() { # name, needle, haystack
  if grep -qF -- "$2" <<<"$3"; then
    echo "PASS  $1"; pass=$((pass+1))
  else
    echo "FAIL  $1"
    echo "  wanted substring: $2"
    sed 's/^/  | /' <<<"$3" | tail -12
    fail=$((fail+1))
  fi
}

refute() { # name, needle, haystack
  if grep -qF -- "$2" <<<"$3"; then
    echo "FAIL  $1 (found forbidden: $2)"; fail=$((fail+1))
  else
    echo "PASS  $1"; pass=$((pass+1))
  fi
}

expect_status() { # name, want
  if [ "$LAST_STATUS" -eq "$2" ]; then
    echo "PASS  $1"; pass=$((pass+1))
  else
    echo "FAIL  $1 (exit=$LAST_STATUS, want=$2)"; fail=$((fail+1))
  fi
}

# --- full CRUD in one session -----------------------------------------------
new_scratch
run \
  "CREATE TABLE users" \
  "ADD COLUMN users name STRING" \
  "ADD COLUMN users age INT" \
  "INSERT INTO users" "Alice" "30" \
  "SELECT * FROM users" \
  "SELECT * FROM users WHERE id=1" \
  "UPDATE users SET name=Bob WHERE id=1" \
  "SELECT * FROM users WHERE id=1" \
  "DELETE FROM users WHERE id=1" \
  "SELECT * FROM users" \
  "QUIT"
expect "table creation"            "Table 'users' created successfully."   "$OUT"
expect "insert echoes new row id"  "Enter values for row 1"                "$OUT"
expect "select shows inserted row" "Alice"                                 "$OUT"
expect "where id=1 finds the row"  "Row ID: 1"                             "$OUT"
expect "update rewrites the value" "Bob"                                   "$OUT"
expect "delete removes the row"    "Row deleted successfully."             "$OUT"
expect "table empty after delete"  "Table is empty."                       "$OUT"
expect_status "session exits cleanly" 0

# --- persistence across restarts --------------------------------------------
new_scratch
run "CREATE TABLE items" \
    "ADD COLUMN items title STRING" \
    "INSERT INTO items" "Widget" \
    "QUIT"
run "LIST TABLES" "SELECT * FROM items" "QUIT"
expect "QUIT persists to disk"      "1 table(s) found"  "$OUT"
expect "rows survive a restart"     "Widget"            "$OUT"
expect_status "reload exits cleanly" 0

# --- DROP TABLE must not corrupt the heap (regression) ----------------------
new_scratch
run "CREATE TABLE temp" "DROP TABLE temp" "CREATE TABLE temp2" "QUIT"
expect "drop reports success"       "Table 'temp' dropped successfully." "$OUT"
refute "no allocator diagnostics"   "free()"                             "$OUT"
expect_status "drop then exit is clean" 0

# --- error handling ----------------------------------------------------------
new_scratch
run \
  "CREATE TABLE dup" \
  "CREATE TABLE dup" \
  "ADD COLUMN dup name STRING" \
  "ADD COLUMN dup name STRING" \
  "SELECT * FROM missing" \
  "BOGUS COMMAND" \
  "QUIT"
expect "duplicate table rejected"   "already exists"          "$OUT"
expect "duplicate column rejected"  "Failed to add column"    "$OUT"
expect "missing table reported"     "Table 'missing' not found." "$OUT"
expect "unknown command reported"   "Unknown command: BOGUS"  "$OUT"
expect_status "error paths exit cleanly" 0

echo
echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ]
