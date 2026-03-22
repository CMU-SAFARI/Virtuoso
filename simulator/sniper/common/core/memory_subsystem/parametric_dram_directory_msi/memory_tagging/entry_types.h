#include<iostream>

enum table_entry_type{
    BITVECTOR,
    RLE,
    TABLE_POINTER,
    NONE,
    NUMBER_OF_ENTRY_TYPES
};
struct table_entry {
    table_entry_type entry_type;
    struct table* next_level_table;
    char* bitvector;
    char* rle;
};
struct table{
    struct table_entry* entries;
    int table_size;
};
table* InitiateTable(int size);
table_entry* CreateNewEntryAtLevel(int level,int number_of_levels,int *level_percentages,int *level_bit_indices);