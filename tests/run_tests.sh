#!/usr/bin/env bash
# Black-box integration tests for the database REPL. Each test runs the
# built binary against a temp working directory (so database.dat/.idx
# never touch the real repo) and asserts on stdout. No framework beyond
# bash + grep, matching the project's own dependency-free style.
set -u

BIN="$(cd "$(dirname "$0")/.." && pwd)/bin/database"
FAILURES=0
ALL_TESTDIRS=()
cleanup() { for d in "${ALL_TESTDIRS[@]:-}"; do [ -n "$d" ] && rm -rf "$d"; done; }
trap cleanup EXIT

# Each test gets its own fresh database.dat/.idx so tests can't see each
# other's tables/rows -- only the two (or more) `run` calls *within* a
# single test share state, which is what the reload/duplicate-create
# tests are actually exercising.
new_testdir() {
  TESTDIR="$(mktemp -d)"
  ALL_TESTDIRS+=("$TESTDIR")
}

check() {
  local desc="$1" haystack="$2" needle="$3"
  if echo "$haystack" | grep -qF -- "$needle"; then
    echo "  ok   - $desc"
  else
    echo "  FAIL - $desc (expected to find: $needle)"
    FAILURES=$((FAILURES + 1))
  fi
}

run() {
  # run <commands...> -- feeds each remaining arg as a line of stdin.
  (cd "$TESTDIR" && printf '%s\n' "$@" | "$BIN" 2>&1)
}

echo "test_create_table_and_columns"

new_testdir
out=$(run "CREATE TABLE users" "ADD COLUMN users name STRING" "ADD COLUMN users age INT" "DESCRIBE users" "QUIT")
check "table created" "$out" "Table 'users' created successfully."
check "column name added" "$out" "Column 'name' added to table 'users'."
check "column age added" "$out" "Column 'age' added to table 'users'."
check "describe shows both columns" "$out" "Columns (2):"

echo "test_insert_and_select"

new_testdir
out=$(run "CREATE TABLE users" "ADD COLUMN users name STRING" "INSERT INTO users" "Alice" "SELECT * FROM users" "QUIT")
check "row inserted" "$out" "Row inserted successfully."
check "select shows the row" "$out" "Alice"

echo "test_select_where_specific_row"

new_testdir
out=$(run "CREATE TABLE users" "ADD COLUMN users name STRING" "INSERT INTO users" "Alice" "INSERT INTO users" "Bob" "SELECT * FROM users WHERE id=1" "QUIT")
check "select where finds row 1" "$out" "Row ID: 1"
check "select where shows Alice" "$out" "name: Alice"

echo "test_update_row"

new_testdir
out=$(run "CREATE TABLE users" "ADD COLUMN users name STRING" "INSERT INTO users" "Alice" "UPDATE users SET name=Alicia WHERE id=1" "SELECT * FROM users WHERE id=1" "QUIT")
check "update reports success" "$out" "Row updated successfully."
check "select shows updated value" "$out" "name: Alicia"

echo "test_delete_row"

new_testdir
out=$(run "CREATE TABLE users" "ADD COLUMN users name STRING" "INSERT INTO users" "Alice" "DELETE FROM users WHERE id=1" "SELECT * FROM users WHERE id=1" "QUIT")
check "delete reports success" "$out" "Row deleted successfully."
check "row no longer found" "$out" "Row with id=1 not found."

echo "test_drop_table"

new_testdir
out=$(run "CREATE TABLE users" "DROP TABLE users" "LIST TABLES" "QUIT")
check "drop reports success" "$out" "Table 'users' dropped successfully."
check "list no longer shows it" "$out" "No tables found."

echo "test_persistence_round_trip_does_not_duplicate_columns"

new_testdir
run "CREATE TABLE users" "ADD COLUMN users name STRING" "ADD COLUMN users age INT" "INSERT INTO users" "Alice" "30" "SAVE" "QUIT" >/dev/null
out=$(run "DESCRIBE users" "SELECT * FROM users" "QUIT")
check "reload keeps exactly 2 columns (regression: used to double to 4)" "$out" "Columns (2):"
check "reloaded row data is intact, not garbage" "$out" "Alice"

echo "test_duplicate_create_table_is_rejected_cleanly"

new_testdir
run "CREATE TABLE users" "SAVE" "QUIT" >/dev/null
out=$(run "CREATE TABLE users" "LIST TABLES" "QUIT")
check "duplicate create is rejected" "$out" "Error: Table already exists or creation failed."
check "original table still intact" "$out" "users (0 rows)"

echo "test_unknown_table_and_unknown_command_are_handled_gracefully"

new_testdir
out=$(run "SELECT * FROM ghost" "NONSENSE" "QUIT")
check "unknown table reported, not a crash" "$out" "Table 'ghost' not found."
check "unknown command reported" "$out" "Unknown command: NONSENSE"

echo ""
if [ "$FAILURES" -eq 0 ]; then
  echo "All tests passed."
  exit 0
else
  echo "$FAILURES check(s) FAILED."
  exit 1
fi
