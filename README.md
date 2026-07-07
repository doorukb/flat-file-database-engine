# Flat File Database Engine

[![CI](https://github.com/doorukb/database/actions/workflows/ci.yml/badge.svg)](https://github.com/doorukb/database/actions/workflows/ci.yml)

A small in-memory, file-persisted database engine with an interactive
command shell : tables with typed columns, hash-indexed rows, and a SQL-ish
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
