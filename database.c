#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

// ============================================================================
// CONSTANTS AND TYPES
// ============================================================================

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

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

uint32_t hash(uint32_t id) {
    return id % HASH_TABLE_SIZE;
}

void trim_newline(char* str) {
    int len = strlen(str);
    if (len > 0 && str[len - 1] == '\n') {
        str[len - 1] = '\0';
    }
}

/* Bounded string copy that always null-terminates `dst` (a buffer of
 * `dst_size` bytes), truncating `src` if it doesn't fit. Replaces the old
 * "copy n-1 bytes, then manually set the last byte to NUL by hand" pattern
 * that used to appear at every call site below -- functionally fine, but
 * it's exactly the shape GCC's -Wstringop-truncation warns about, since
 * the underlying copy not null-terminating its destination on its own is
 * what that warning is watching for, regardless of the manual fixup
 * after it. */
static void safe_copy(char* dst, const char* src, size_t dst_size) {
    snprintf(dst, dst_size, "%s", src);
}

// ============================================================================
// ROW OPERATIONS
// ============================================================================

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
        return false; // Row already exists
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

// ============================================================================
// TABLE OPERATIONS
// ============================================================================

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

void free_table(Table* table) {
    if (!table) return;
    
    // Free all rows
    for (int i = 0; i < HASH_TABLE_SIZE; i++) {
        RowNode* current = table->index[i].rows;
        while (current) {
            RowNode* next = current->next;
            free(current);
            current = next;
        }
    }
    
    free(table->index);
    if (table->columns) {
        free(table->columns);
    }
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

// ============================================================================
// DATABASE OPERATIONS
// ============================================================================

Database* create_database() {
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
        free_table(&db->tables[i]);
    }
    
    free(db->tables);
    free(db);
}

bool add_table(Database* db, Table* table) {
    if (find_table(db, table->name)) {
        return false; // Table already exists
    }
    
    if (db->table_count >= db->capacity) {
        db->capacity *= 2;
        db->tables = (Table*)realloc(db->tables, db->capacity * sizeof(Table));
        if (!db->tables) return false;
    }
    
    // Move table into database
    db->tables[db->table_count] = *table;
    free(table); // Free the temporary table pointer
    db->table_count++;
    
    return true;
}

// ============================================================================
// FILE PERSISTENCE
// ============================================================================

bool save_database(Database* db) {
    FILE* file = fopen(DATA_FILE, "wb");
    if (!file) return false;
    
    // Write table count
    fwrite(&db->table_count, sizeof(int), 1, file);
    
    for (int t = 0; t < db->table_count; t++) {
        Table* table = &db->tables[t];
        
        // Write table metadata
        int name_len = strlen(table->name);
        fwrite(&name_len, sizeof(int), 1, file);
        fwrite(table->name, sizeof(char), name_len, file);
        fwrite(&table->column_count, sizeof(int), 1, file);
        fwrite(&table->next_id, sizeof(uint32_t), 1, file);
        fwrite(&table->row_count, sizeof(int), 1, file);
        
        // Write columns
        for (int c = 0; c < table->column_count; c++) {
            int col_name_len = strlen(table->columns[c].name);
            fwrite(&col_name_len, sizeof(int), 1, file);
            fwrite(table->columns[c].name, sizeof(char), col_name_len, file);
            fwrite(&table->columns[c].type, sizeof(ColumnType), 1, file);
        }
        
        // Write rows
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

Database* load_database() {
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
        // Read table metadata
        int name_len;
        if (fread(&name_len, sizeof(int), 1, file) != 1) break;
        
        if (name_len < 0 || name_len >= MAX_TABLE_NAME) {
            break; // corrupt or foreign file -- would overflow table_name below
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
        
        // NOTE: column_count is intentionally *not* pre-set here. add_column()
        // below uses table->column_count as the next write index and then
        // increments it itself (the same contract every other caller relies
        // on, e.g. the "ADD COLUMN" command). Pre-setting it to the final
        // count before the loop -- as this used to do -- made every reload
        // double the column count: the first add_column() call wrote into
        // slot [column_count] instead of slot [0], leaving slot 0 (and every
        // slot up to the real data) as uninitialized heap garbage that then
        // got treated as a real column for the rest of the program's life.
        table->next_id = next_id;
        
        // Read columns
        for (int c = 0; c < column_count; c++) {
            int col_name_len;
            if (fread(&col_name_len, sizeof(int), 1, file) != 1) break;
            
            if (col_name_len < 0 || col_name_len >= MAX_COLUMN_NAME) {
                // Corrupt or foreign file -- refuse to read past the
                // stack buffer below rather than overflowing it with
                // whatever length value was on disk.
                break;
            }
            
            char col_name[MAX_COLUMN_NAME];
            if (fread(col_name, sizeof(char), (size_t)col_name_len, file) != (size_t)col_name_len) break;
            col_name[col_name_len] = '\0';
            
            ColumnType type;
            if (fread(&type, sizeof(ColumnType), 1, file) != 1) break;
            
            add_column(table, col_name, type);
        }
        
        // Read rows
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

// ============================================================================
// COMMAND INTERFACE
// ============================================================================

void print_help() {
    printf("\nDatabase Commands:\n");
    printf("  CREATE TABLE <name>                   - Create a new table\n");
    printf("  ADD COLUMN <table> <name> <type>      - Add column (STRING|INT|FLOAT)\n");
    printf("  INSERT INTO <table>                   - Insert a new row\n");
    printf("  SELECT * FROM <table>                 - Show all rows\n");
    printf("  SELECT * FROM <table> WHERE id=<id>   - Show specific row\n");
    printf("  UPDATE <table> SET <col>=<val> WHERE id=<id> - Update row\n");
    printf("  DELETE FROM <table> WHERE id=<id>     - Delete row\n");
    printf("  DROP TABLE <name>                     - Delete a table\n");
    printf("  LIST TABLES                           - List all tables\n");
    printf("  DESCRIBE <table>                      - Show table schema\n");
    printf("  SAVE                                  - Save database to disk\n");
    printf("  QUIT                                  - Exit\n");
    printf("  HELP                                  - Show this help\n\n");
}

void list_tables(Database* db) {
    if (db->table_count == 0) {
        printf("No tables found.\n");
        return;
    }
    
    printf("\nTables:\n");
    for (int i = 0; i < db->table_count; i++) {
        printf("  %s (%d rows)\n", db->tables[i].name, db->tables[i].row_count);
    }
    printf("\n");
}

void describe_table(Table* table) {
    if (!table) {
        printf("Table not found.\n");
        return;
    }
    
    printf("\nTable: %s\n", table->name);
    printf("Columns (%d):\n", table->column_count);
    for (int i = 0; i < table->column_count; i++) {
        const char* type_str = "UNKNOWN";
        switch (table->columns[i].type) {
            case TYPE_STRING: type_str = "STRING"; break;
            case TYPE_INT: type_str = "INT"; break;
            case TYPE_FLOAT: type_str = "FLOAT"; break;
        }
        printf("  %s (%s)\n", table->columns[i].name, type_str);
    }
    printf("Rows: %d\n\n", table->row_count);
}

void select_all(Table* table) {
    if (!table) {
        printf("Table not found.\n");
        return;
    }
    
    if (table->row_count == 0) {
        printf("Table is empty.\n");
        return;
    }
    
    // Print header
    printf("\n");
    printf("%-8s", "ID");
    for (int i = 0; i < table->column_count; i++) {
        printf("%-16s", table->columns[i].name);
    }
    printf("\n");
    printf("-------------------------------------------------------------------\n");
    
    // Print rows
    for (int i = 0; i < HASH_TABLE_SIZE; i++) {
        RowNode* current = table->index[i].rows;
        while (current) {
            printf("%-8u", current->row.id);
            for (int j = 0; j < table->column_count; j++) {
                printf("%-16s", current->row.values[j][0] ? current->row.values[j] : "NULL");
            }
            printf("\n");
            current = current->next;
        }
    }
    printf("\n");
}

void select_where(Table* table, uint32_t id) {
    if (!table) {
        printf("Table not found.\n");
        return;
    }
    
    RowNode* row_node = find_row(table, id);
    if (!row_node) {
        printf("Row with id=%u not found.\n", id);
        return;
    }
    
    printf("\nRow ID: %u\n", row_node->row.id);
    for (int i = 0; i < table->column_count; i++) {
        printf("  %s: %s\n", table->columns[i].name, 
               row_node->row.values[i][0] ? row_node->row.values[i] : "NULL");
    }
    printf("\n");
}

int find_column_index(Table* table, const char* col_name) {
    for (int i = 0; i < table->column_count; i++) {
        if (strcmp(table->columns[i].name, col_name) == 0) {
            return i;
        }
    }
    return -1;
}

void process_command(Database* db, char* command) {
    trim_newline(command);
    
    if (strlen(command) == 0) return;
    
    char cmd[256];
    sscanf(command, "%255s", cmd);
    
    if (strcmp(cmd, "CREATE") == 0) {
        char table_cmd[256], table_name[MAX_TABLE_NAME];
        if (sscanf(command, "%255s %255s %63s", cmd, table_cmd, table_name) == 3 && 
            strcmp(table_cmd, "TABLE") == 0) {
            Table* table = create_table(table_name);
            if (table && add_table(db, table)) {
                printf("Table '%s' created successfully.\n", table_name);
            } else {
                // add_table() only takes ownership of `table` on success
                // (it copies the struct into db->tables and frees just the
                // temporary wrapper itself). On failure -- almost always
                // because a table with this name already exists -- the
                // wrapper *and* the 16KB index it calloc'd in
                // create_table() were never freed by anyone. free_table()
                // releases both.
                if (table) {
                    free_table(table);
                }
                printf("Error: Table already exists or creation failed.\n");
            }
        } else {
            printf("Syntax: CREATE TABLE <name>\n");
        }
    }
    else if (strcmp(cmd, "ADD") == 0) {
        char col_cmd[256], table_name[MAX_TABLE_NAME], col_name[MAX_COLUMN_NAME], type_str[16];
        if (sscanf(command, "%255s %255s %63s %63s %15s", 
                   cmd, col_cmd, table_name, col_name, type_str) == 5 &&
            strcmp(col_cmd, "COLUMN") == 0) {
            Table* table = find_table(db, table_name);
            if (!table) {
                printf("Table '%s' not found.\n", table_name);
                return;
            }
            
            ColumnType type = TYPE_STRING;
            if (strcmp(type_str, "INT") == 0) type = TYPE_INT;
            else if (strcmp(type_str, "FLOAT") == 0) type = TYPE_FLOAT;
            
            if (add_column(table, col_name, type)) {
                printf("Column '%s' added to table '%s'.\n", col_name, table_name);
            } else {
                printf("Error: Failed to add column.\n");
            }
        } else {
            printf("Syntax: ADD COLUMN <table> <name> <type>\n");
        }
    }
    else if (strcmp(cmd, "INSERT") == 0) {
        char into[256], table_name[MAX_TABLE_NAME];
        if (sscanf(command, "%255s %255s %63s", cmd, into, table_name) == 3 &&
            strcmp(into, "INTO") == 0) {
            Table* table = find_table(db, table_name);
            if (!table) {
                printf("Table '%s' not found.\n", table_name);
                return;
            }
            
            uint32_t id = table->next_id++;
            if (insert_row(table, id)) {
                printf("Enter values for row %u:\n", id);
                for (int i = 0; i < table->column_count; i++) {
                    printf("  %s: ", table->columns[i].name);
                    fflush(stdout);
                    char value[MAX_COLUMN_VALUE];
                    if (fgets(value, sizeof(value), stdin)) {
                        trim_newline(value);
                        RowNode* row_node = find_row(table, id);
                        if (row_node) {
                            safe_copy(row_node->row.values[i], value, MAX_COLUMN_VALUE);
                        }
                    }
                }
                printf("Row inserted successfully.\n");
            } else {
                printf("Error: Failed to insert row.\n");
            }
        } else {
            printf("Syntax: INSERT INTO <table>\n");
        }
    }
    else if (strcmp(cmd, "SELECT") == 0) {
        char star[256], from[256], table_name[MAX_TABLE_NAME];
        int scanned = sscanf(command, "%255s %255s %255s %63s", cmd, star, from, table_name);
        
        if (scanned >= 4 && strcmp(from, "FROM") == 0) {
            Table* table = find_table(db, table_name);
            if (!table) {
                printf("Table '%s' not found.\n", table_name);
                return;
            }
            
            // Check for WHERE clause
            char where[256];
            uint32_t id;
            if (sscanf(command, "%*s %*s %*s %*s %255s id=%u", where, &id) == 2 &&
                strcmp(where, "WHERE") == 0) {
                select_where(table, id);
            } else {
                select_all(table);
            }
        } else {
            printf("Syntax: SELECT * FROM <table> [WHERE id=<id>]\n");
        }
    }
    else if (strcmp(cmd, "UPDATE") == 0) {
        char table_name[MAX_TABLE_NAME], set[256], col_name[MAX_COLUMN_NAME], 
             value[MAX_COLUMN_VALUE], where[256];
        uint32_t id;
        
        // Parse: UPDATE <table> SET <col>=<val> WHERE id=<id>
        if (sscanf(command, "%*s %63s %255s %63[^=]=%255[^ ] %255s id=%u",
                   table_name, set, col_name, value, where, &id) == 6 &&
            strcmp(set, "SET") == 0 && strcmp(where, "WHERE") == 0) {
            Table* table = find_table(db, table_name);
            if (!table) {
                printf("Table '%s' not found.\n", table_name);
                return;
            }
            
            RowNode* row_node = find_row(table, id);
            if (!row_node) {
                printf("Row with id=%u not found.\n", id);
                return;
            }
            
            int col_idx = find_column_index(table, col_name);
            if (col_idx < 0) {
                printf("Column '%s' not found.\n", col_name);
                return;
            }
            
            safe_copy(row_node->row.values[col_idx], value, MAX_COLUMN_VALUE);
            printf("Row updated successfully.\n");
        } else {
            printf("Syntax: UPDATE <table> SET <col>=<val> WHERE id=<id>\n");
        }
    }
    else if (strcmp(cmd, "DELETE") == 0) {
        char from[256], table_name[MAX_TABLE_NAME], where[256];
        uint32_t id;
        
        if (sscanf(command, "%*s %255s %63s %255s id=%u",
                   from, table_name, where, &id) == 4 &&
            strcmp(from, "FROM") == 0 && strcmp(where, "WHERE") == 0) {
            Table* table = find_table(db, table_name);
            if (!table) {
                printf("Table '%s' not found.\n", table_name);
                return;
            }
            
            if (delete_row(table, id)) {
                printf("Row deleted successfully.\n");
            } else {
                printf("Row with id=%u not found.\n", id);
            }
        } else {
            printf("Syntax: DELETE FROM <table> WHERE id=<id>\n");
        }
    }
    else if (strcmp(cmd, "DROP") == 0) {
        char table_cmd[256], table_name[MAX_TABLE_NAME];
        if (sscanf(command, "%255s %255s %63s", cmd, table_cmd, table_name) == 3 &&
            strcmp(table_cmd, "TABLE") == 0) {
            // Find and remove table
            for (int i = 0; i < db->table_count; i++) {
                if (strcmp(db->tables[i].name, table_name) == 0) {
                    free_table(&db->tables[i]);
                    // Shift remaining tables
                    for (int j = i; j < db->table_count - 1; j++) {
                        db->tables[j] = db->tables[j + 1];
                    }
                    db->table_count--;
                    printf("Table '%s' dropped successfully.\n", table_name);
                    return;
                }
            }
            printf("Table '%s' not found.\n", table_name);
        } else {
            printf("Syntax: DROP TABLE <name>\n");
        }
    }
    else if (strcmp(cmd, "LIST") == 0) {
        list_tables(db);
    }
    else if (strcmp(cmd, "DESCRIBE") == 0) {
        char table_name[MAX_TABLE_NAME];
        if (sscanf(command, "%*s %63s", table_name) == 1) {
            Table* table = find_table(db, table_name);
            describe_table(table);
        } else {
            printf("Syntax: DESCRIBE <table>\n");
        }
    }
    else if (strcmp(cmd, "SAVE") == 0) {
        if (save_database(db)) {
            printf("Database saved successfully.\n");
        } else {
            printf("Error: Failed to save database.\n");
        }
    }
    else if (strcmp(cmd, "HELP") == 0) {
        print_help();
    }
    else if (strcmp(cmd, "QUIT") == 0 || strcmp(cmd, "EXIT") == 0) {
        printf("Goodbye!\n");
        exit(0);
    }
    else {
        printf("Unknown command: %s\n", cmd);
        printf("Type HELP for available commands.\n");
    }
}

// ============================================================================
// MAIN
// ============================================================================

int main() {
    printf("=== Simple Database System ===\n");
    printf("Type HELP for commands, QUIT to exit\n\n");
    
    Database* db = load_database();
    if (!db) {
        printf("Error: Failed to initialize database.\n");
        return 1;
    }
    
    printf("Database loaded. %d table(s) found.\n\n", db->table_count);
    
    char command[512];
    while (true) {
        printf("db> ");
        fflush(stdout);
        
        if (!fgets(command, sizeof(command), stdin)) {
            break;
        }
        
        process_command(db, command);
    }
    
    // Auto-save on exit
    save_database(db);
    free_database(db);
    
    return 0;
}
