#include "entry_types.h"
#include <math.h> 
#include <stdlib.h>
#include <time.h> 
table_entry* CreateNewEntryAtLevel(int level,int number_of_levels,int *level_percentages,int *level_bit_indices){
    table_entry* e=(table_entry*)malloc(sizeof(table_entry));
    if(level!=number_of_levels){
        int numb=rand()%100;
        if(numb<level_percentages[level-1]){
            e->rle=(char*)malloc(8);
            e->entry_type=table_entry_type::RLE;
        }
        else{
            e->entry_type=table_entry_type::TABLE_POINTER;
            
            e->next_level_table=InitiateTable((int)pow(2.0,(float)level_bit_indices[level]));
            
        }
    }
    else{
        int numb=rand()%100;
        if(numb>20){
            e->rle=(char*)malloc(8);
            e->entry_type=table_entry_type::RLE;
        }
        else{
            e->entry_type=table_entry_type::BITVECTOR;
            e->bitvector=(char*)malloc((int)pow(2.0,(float)(level_bit_indices[level]-1)));
        }
    }
    return e;
}
table* InitiateTable(int size){
    
    table* t=(table*)malloc(sizeof(table));
    t->table_size=size;
    
    t->entries=(table_entry*)malloc(size*sizeof(table_entry));
    for (int i = 0; i < size; i++)
    {
        t->entries[i].entry_type=table_entry_type::NONE;
    }
    
    return t;
}