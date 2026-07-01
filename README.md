# Simple Database

A small in-memory, file-persisted database engine with an interactive
command shell — tables with typed columns, hash-indexed rows, and a SQL-ish
command language (`CREATE TABLE`, `INSERT`, `SELECT ... WHERE`, `UPDATE`,
`DELETE`, `DROP TABLE`), saved to disk and reloaded on startup.

## Build & run

```sh
make            # builds bin/database
make test        # builds and runs the integration test suite
./bin/database
```

```
$ ./bin/database
=== Simple Database System ===
Type HELP for commands, QUIT to exit

Database loaded. 0 table(s) found.

db> CREATE TABLE users
Table 'users' created successfully.
db> ADD COLUMN users name STRING
Column 'name' added to table 'users'.
db> INSERT INTO users
Enter values for row 1:
  name: Alice
Row inserted successfully.
db> SELECT * FROM users
ID      name
-------------------------------------------------------------------
1       Alice

db> SAVE
Database saved successfully.
db> QUIT
```

Run `HELP` inside the shell for the full command list. Data is persisted
to `database.dat` in the current directory on `SAVE` and on exit, and
reloaded automatically on the next run.

## How it works

- **Storage.** Each `Table` owns a 1024-bucket hash index
  (`id % 1024 -> linked list of rows`) for O(1)-average row lookup by id,
  plus a flat array of typed `Column` definitions.
- **Database growth.** Tables live in a single contiguous, growable array
  (`Database.tables`), doubling capacity via `realloc()` when it fills up
  — `add_table()` copies a fully-built `Table` struct in by value and
  frees only the temporary wrapper it arrived in, so existing tables'
  index/column allocations are untouched by the array growing.
- **Persistence.** `save_database()`/`load_database()` write/read a
  straightforward length-prefixed binary format: table count, then per
  table its name, column definitions, and rows, each string preceded by
  its byte length.

## Bugs fixed in this pass

The most serious ones only showed up after a save/reload cycle — i.e. on
the most basic real-world use of a persistent database (use it, restart,
keep using it):

- **Every reload doubled the table's column count.** `load_database()`
  set `table->column_count` to its final value *before* calling
  `add_column()` for each stored column. `add_column()` writes into slot
  `[column_count]` and increments it itself — the same contract the live
  `ADD COLUMN` command relies on — so pre-setting the count made the
  first `add_column()` call write into the *second* slot, leaving the
  first slot (and everything up to where real data started) as
  uninitialized heap garbage that was then treated as a real column for
  the rest of the program's life (visible as garbled bytes when the next
  `INSERT` prompted for that column). Now `add_column()` builds the count
  up from zero on load exactly like it does everywhere else.
- **Stack buffer overflow reading table/column names from disk.**
  `load_database()` read a length prefix off disk and immediately
  `fread()`'d that many bytes into a fixed 64-byte stack buffer with no
  bounds check — a corrupted or hand-edited `database.dat` (including one
  corrupted by the bug above) overflows it. Both lengths are now
  validated against their buffer sizes before reading.
- **Leaked a table (and its 16KB index) on every duplicate `CREATE
  TABLE`.** `add_table()` only takes ownership of the `Table` it's given
  on success; on failure (almost always "already exists") neither it nor
  the caller ever freed the temporary table `create_table()` had already
  allocated. Now freed via `free_table()` on that path.

All three were reproduced and verified fixed under
`-fsanitize=address,undefined` across a full
create → populate → save → reload → duplicate-create → insert → exit
cycle (see `tests/run_tests.sh`).

## Tests

`tests/run_tests.sh` is a dependency-free black-box suite that drives the
built binary over stdin the way a real session would: table/column
creation, insert/select/update/delete, `DROP TABLE`, unknown
table/command handling, and — the two regression tests that matter most
— a save/reload round trip asserting the column count *doesn't* double,
and a duplicate `CREATE TABLE` asserting the original table survives
untouched. Run with `make test`.

## Project layout

```
database.c          everything: types, table/row ops, persistence, REPL
tests/run_tests.sh   black-box integration test suite
Makefile
```
