#include "database.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t hash(uint32_t id) {
    return id % HASH_TABLE_SIZE;
}

static void safe_copy(char* dst, const char* src, size_t dst_size) {
    snprintf(dst, dst_size, "%s", src);
}

RowNode* create_row_node(uint32_t id) {
    RowNode* node = (RowNode*)malloc(sizeof(RowNode));
    if (!node) return NULL;

    node->row.id = id;
    memset(node->row.values, 0, sizeof(node->row.values));
    node->next = NULL;
    return node;
}

RowNode* find_row(Table* table, uint32_t id) {
    uint32_t bucket_idx = hash(id);
    HashBucket* bucket = &table->index[bucket_idx];
    RowNode* current = bucket->rows;

    while (current) {
        if (current->row.id == id) {
            return current;
        }
        current = current->next;
    }
    return NULL;
}

bool insert_row(Table* table, uint32_t id) {
    RowNode* existing = find_row(table, id);
    if (existing) {
        return false;
    }

    RowNode* new_node = create_row_node(id);
    if (!new_node) return false;

    uint32_t bucket_idx = hash(id);
    HashBucket* bucket = &table->index[bucket_idx];

    new_node->next = bucket->rows;
    bucket->rows = new_node;
    table->row_count++;

    return true;
}

bool delete_row(Table* table, uint32_t id) {
    uint32_t bucket_idx = hash(id);
    HashBucket* bucket = &table->index[bucket_idx];

    RowNode* current = bucket->rows;
    RowNode* prev = NULL;

    while (current) {
        if (current->row.id == id) {
            if (prev) {
                prev->next = current->next;
            } else {
                bucket->rows = current->next;
            }
            free(current);
            table->row_count--;
            return true;
        }
        prev = current;
        current = current->next;
    }
    return false;
}

Table* create_table(const char* name) {
    Table* table = (Table*)malloc(sizeof(Table));
    if (!table) return NULL;

    safe_copy(table->name, name, MAX_TABLE_NAME);
    table->columns = NULL;
    table->column_count = 0;
    table->index = (HashBucket*)calloc(HASH_TABLE_SIZE, sizeof(HashBucket));
    table->next_id = 1;
    table->row_count = 0;

    if (!table->index) {
        free(table);
        return NULL;
    }

    return table;
}

/* Free everything a table owns (rows, index, columns) but not the Table
 * struct itself. Needed because tables live in two kinds of storage: as
 * heap-allocated shells fresh out of create_table(), and as elements copied
 * into the database's tables array by add_table(). */
void free_table_contents(Table* table) {
    if (!table) return;

    for (int i = 0; i < HASH_TABLE_SIZE; i++) {
        RowNode* current = table->index[i].rows;
        while (current) {
            RowNode* next = current->next;
            free(current);
            current = next;
        }
    }

    free(table->index);
    table->index = NULL;
    if (table->columns) {
        free(table->columns);
        table->columns = NULL;
    }
}

/* Free a heap-allocated table (from create_table) including the struct */
void free_table(Table* table) {
    if (!table) return;
    free_table_contents(table);
    free(table);
}

Table* find_table(Database* db, const char* name) {
    for (int i = 0; i < db->table_count; i++) {
        if (strcmp(db->tables[i].name, name) == 0) {
            return &db->tables[i];
        }
    }
    return NULL;
}

bool add_column(Table* table, const char* name, ColumnType type) {
    if (table->column_count >= MAX_COLUMNS) {
        return false;
    }

    if (!table->columns) {
        table->columns = (Column*)malloc(MAX_COLUMNS * sizeof(Column));
        if (!table->columns) return false;
    }

    Column* col = &table->columns[table->column_count];
    safe_copy(col->name, name, MAX_COLUMN_NAME);
    col->type = type;
    table->column_count++;

    return true;
}

Database* create_database(void) {
    Database* db = (Database*)malloc(sizeof(Database));
    if (!db) return NULL;

    db->capacity = 10;
    db->table_count = 0;
    db->tables = (Table*)malloc(db->capacity * sizeof(Table));

    if (!db->tables) {
        free(db);
        return NULL;
    }

    return db;
}

void free_database(Database* db) {
    if (!db) return;

    for (int i = 0; i < db->table_count; i++) {
        free_table_contents(&db->tables[i]);
    }

    free(db->tables);
    free(db);
}

bool add_table(Database* db, Table* table) {
    if (find_table(db, table->name)) {
        return false;
    }

    if (db->table_count >= db->capacity) {
        int new_capacity = db->capacity * 2;
        Table* grown = (Table*)realloc(db->tables, new_capacity * sizeof(Table));
        if (!grown) return false;  /* keep the old array usable on failure */
        db->tables = grown;
        db->capacity = new_capacity;
    }

    db->tables[db->table_count] = *table;
    free(table);
    db->table_count++;

    return true;
}

bool save_database(Database* db) {
    FILE* file = fopen(DATA_FILE, "wb");
    if (!file) return false;

    fwrite(&db->table_count, sizeof(int), 1, file);

    for (int t = 0; t < db->table_count; t++) {
        Table* table = &db->tables[t];

        int name_len = strlen(table->name);
        fwrite(&name_len, sizeof(int), 1, file);
        fwrite(table->name, sizeof(char), name_len, file);
        fwrite(&table->column_count, sizeof(int), 1, file);
        fwrite(&table->next_id, sizeof(uint32_t), 1, file);
        fwrite(&table->row_count, sizeof(int), 1, file);

        for (int c = 0; c < table->column_count; c++) {
            int col_name_len = strlen(table->columns[c].name);
            fwrite(&col_name_len, sizeof(int), 1, file);
            fwrite(table->columns[c].name, sizeof(char), col_name_len, file);
            fwrite(&table->columns[c].type, sizeof(ColumnType), 1, file);
        }

        for (int i = 0; i < HASH_TABLE_SIZE; i++) {
            RowNode* current = table->index[i].rows;
            while (current) {
                fwrite(&current->row.id, sizeof(uint32_t), 1, file);
                for (int v = 0; v < table->column_count; v++) {
                    int val_len = strlen(current->row.values[v]);
                    fwrite(&val_len, sizeof(int), 1, file);
                    if (val_len > 0) {
                        fwrite(current->row.values[v], sizeof(char), val_len, file);
                    }
                }
                current = current->next;
            }
        }
    }

    fclose(file);
    return true;
}

Database* load_database(void) {
    FILE* file = fopen(DATA_FILE, "rb");
    if (!file) {
        return create_database();
    }

    Database* db = create_database();
    if (!db) {
        fclose(file);
        return NULL;
    }

    int table_count;
    if (fread(&table_count, sizeof(int), 1, file) != 1) {
        fclose(file);
        return db;
    }

    for (int t = 0; t < table_count; t++) {
        int name_len;
        if (fread(&name_len, sizeof(int), 1, file) != 1) break;

        if (name_len < 0 || name_len >= MAX_TABLE_NAME) {
            break;
        }

        char table_name[MAX_TABLE_NAME];
        if (fread(table_name, sizeof(char), (size_t)name_len, file) != (size_t)name_len) break;
        table_name[name_len] = '\0';

        int column_count;
        uint32_t next_id;
        int row_count;
        if (fread(&column_count, sizeof(int), 1, file) != 1) break;
        if (fread(&next_id, sizeof(uint32_t), 1, file) != 1) break;
        if (fread(&row_count, sizeof(int), 1, file) != 1) break;

        Table* table = create_table(table_name);
        if (!table) break;

        table->next_id = next_id;

        for (int c = 0; c < column_count; c++) {
            int col_name_len;
            if (fread(&col_name_len, sizeof(int), 1, file) != 1) break;

            if (col_name_len < 0 || col_name_len >= MAX_COLUMN_NAME) {
                break;
            }

            char col_name[MAX_COLUMN_NAME];
            if (fread(col_name, sizeof(char), (size_t)col_name_len, file) != (size_t)col_name_len) break;
            col_name[col_name_len] = '\0';

            ColumnType type;
            if (fread(&type, sizeof(ColumnType), 1, file) != 1) break;

            add_column(table, col_name, type);
        }

        for (int r = 0; r < row_count; r++) {
            uint32_t id;
            if (fread(&id, sizeof(uint32_t), 1, file) != 1) break;

            insert_row(table, id);
            RowNode* row_node = find_row(table, id);

            for (int v = 0; v < column_count; v++) {
                int val_len;
                if (fread(&val_len, sizeof(int), 1, file) != 1) break;

                if (val_len > 0 && val_len < MAX_COLUMN_VALUE) {
                    if (fread(row_node->row.values[v], sizeof(char), (size_t)val_len, file) != (size_t)val_len) break;
                    row_node->row.values[v][val_len] = '\0';
                }
            }
        }

        add_table(db, table);
    }

    fclose(file);
    return db;
}