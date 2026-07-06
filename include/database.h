#ifndef DATABASE_H
#define DATABASE_H

#include <stdbool.h>
#include <stdint.h>

#define MAX_COLUMN_NAME 64
#define MAX_COLUMN_VALUE 256
#define MAX_TABLE_NAME 64
#define MAX_COLUMNS 32
#define HASH_TABLE_SIZE 1024
#define DATA_FILE "database.dat"
#define INDEX_FILE "database.idx"

typedef enum {
    TYPE_STRING,
    TYPE_INT,
    TYPE_FLOAT
} ColumnType;

typedef struct {
    char name[MAX_COLUMN_NAME];
    ColumnType type;
} Column;

typedef struct {
    uint32_t id;
    char values[MAX_COLUMNS][MAX_COLUMN_VALUE];
} Row;

typedef struct RowNode {
    Row row;
    struct RowNode* next;
} RowNode;

typedef struct {
    uint32_t id;
    RowNode* rows;
} HashBucket;

typedef struct {
    char name[MAX_TABLE_NAME];
    Column* columns;
    int column_count;
    HashBucket* index;
    uint32_t next_id;
    int row_count;
} Table;

typedef struct {
    Table* tables;
    int table_count;
    int capacity;
} Database;

RowNode* create_row_node(uint32_t id);
RowNode* find_row(Table* table, uint32_t id);
bool insert_row(Table* table, uint32_t id);
bool delete_row(Table* table, uint32_t id);

Table* create_table(const char* name);
void free_table(Table* table);
void free_table_contents(Table* table);
Table* find_table(Database* db, const char* name);
bool add_column(Table* table, const char* name, ColumnType type);

Database* create_database(void);
void free_database(Database* db);
bool add_table(Database* db, Table* table);

bool save_database(Database* db);
Database* load_database(void);

#endif