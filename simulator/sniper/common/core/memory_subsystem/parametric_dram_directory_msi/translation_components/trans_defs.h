#ifndef __TRANS_TYPES_H
#define __TRANS_TYPES_H
    
typedef struct queue_entry_s
{
    SubsecondTime timestamp;
    int page_size;
    IntPtr ppn;
    IntPtr address;
} query_entry;


class Compare
{
public:
    bool operator()(query_entry a, query_entry b)
    {
        return a.timestamp > b.timestamp;
    }
};

#endif // __