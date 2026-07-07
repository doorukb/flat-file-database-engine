#include "database.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void trim_newline(char* str) {
    int len = strlen(str);
    if (len > 0 && str[len - 1] == '\n') {
        str[len - 1] = '\0';
    }
}

static void safe_copy(char* dst, const char* src, size_t dst_size) {
    snprintf(dst, dst_size, "%s", src);
}

static void print_help(void) {
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

static void list_tables(Database* db) {
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

static void describe_table(Table* table) {
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

static void select_all(Table* table) {
    if (!table) {
        printf("Table not found.\n");
        return;
    }

    if (table->row_count == 0) {
        printf("Table is empty.\n");
        return;
    }

    printf("\n");
    printf("%-8s", "ID");
    for (int i = 0; i < table->column_count; i++) {
        printf("%-16s", table->columns[i].name);
    }
    printf("\n");
    printf("-------------------------------------------------------------------\n");

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

static void select_where(Table* table, uint32_t id) {
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

static int find_column_index(Table* table, const char* col_name) {
    for (int i = 0; i < table->column_count; i++) {
        if (strcmp(table->columns[i].name, col_name) == 0) {
            return i;
        }
    }
    return -1;
}

/* Returns false when the user asked to quit */
static bool process_command(Database* db, char* command) {
    trim_newline(command);

    if (strlen(command) == 0) return true;

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
                return true;
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
                return true;
            }

            uint32_t id = table->next_id;
            if (insert_row(table, id)) {
                table->next_id++;
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
                return true;
            }

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

        if (sscanf(command, "%*s %63s %255s %63[^=]=%255[^ ] %255s id=%u",
                   table_name, set, col_name, value, where, &id) == 6 &&
            strcmp(set, "SET") == 0 && strcmp(where, "WHERE") == 0) {
            Table* table = find_table(db, table_name);
            if (!table) {
                printf("Table '%s' not found.\n", table_name);
                return true;
            }

            RowNode* row_node = find_row(table, id);
            if (!row_node) {
                printf("Row with id=%u not found.\n", id);
                return true;
            }

            int col_idx = find_column_index(table, col_name);
            if (col_idx < 0) {
                printf("Column '%s' not found.\n", col_name);
                return true;
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
                return true;
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
            for (int i = 0; i < db->table_count; i++) {
                if (strcmp(db->tables[i].name, table_name) == 0) {
                    free_table_contents(&db->tables[i]);
                    for (int j = i; j < db->table_count - 1; j++) {
                        db->tables[j] = db->tables[j + 1];
                    }
                    db->table_count--;
                    printf("Table '%s' dropped successfully.\n", table_name);
                    return true;
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
        return false;
    }
    else {
        printf("Unknown command: %s\n", cmd);
        printf("Type HELP for available commands.\n");
    }
    return true;
}

int main(void) {
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

        if (!process_command(db, command)) {
            break;
        }
    }

    save_database(db);
    free_database(db);

    return 0;
}