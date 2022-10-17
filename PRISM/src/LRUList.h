#ifndef MTS_LRUList_H
#define MTS_LRUList_H

#include <stdlib.h>
#include <bitset>
#include <vector>
#include <list>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <atomic>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <libpmem.h>
#include <libpmemobj.h>
#include "MTSImpl.h"

typedef struct at_entry at_entry_t;

typedef struct cq_entry {
    at_entry_t *at_entry;
    Key_t key;
    Val_t val;
    bool ops;
    void init(at_entry_t *_at_entry, Val_t _val, bool _ops) {
	this->val = _val;
	this->at_entry = _at_entry;
	this->ops = _ops;
    }
} cq_entry_t;

typedef struct dc_entry {
    at_entry_t *at_entry;
    dc_entry *prev, *next;
    dc_entry *s_prev, *s_next;
    unsigned int list_type;

    Key_t key;
    Val_t val;
    unsigned char __reserved[KV_SIZE-sizeof(Val_t)-sizeof(Key_t)];
} dc_entry_t;

typedef struct cache {
    dc_entry_t *head, *tail; // Doubly-linked list
    int max_size; // Maxiumum number of entries
    int cur_size; // Current number of entries
    unsigned int list_type;
} cache_t;

class LRUList {
    private:

    public:
	LRUList();
	LRUList(unsigned int list_type);
	~LRUList();

	cache_t *dcache;

	dc_entry_t *alloc_entry(cq_entry_t *cq_entry);
	void free_entry(dc_entry_t *dc_entry);
	void insert_head(dc_entry_t *dc_entry);
	void move_to_head(dc_entry_t *dc_entry);
	void move_to_tail(dc_entry_t* dc_entry);

	dc_entry_t *get_tail();
	dc_entry_t *remove_tail();
	dc_entry_t *remove_entry(dc_entry_t *dc_entry);

	bool chain_has_active_entry(dc_entry_t *dc_entry);

	int get_cur_size();
	int get_max_size();

	dc_entry_t *iter_dcache();
};

#endif /* MTS_LRUList_H */
