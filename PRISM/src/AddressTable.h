#ifndef MTS_ADDRESSTABLE_H
#define MTS_ADDRESSTABLE_H

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
#include "SpinLock.h"

class ValueStorage;
typedef struct mts_op_entry op_entry_t;
typedef struct dc_entry dc_entry_t;

typedef struct at_idx {
    unsigned int at_id;
    uint32_t at_offset;
} at_idx_t;

typedef struct vs_idx {
    int vs_id;
    int vs_offset;
} vs_idx_t;

typedef struct dummy_entry{
    Key_t key;
    Val_t val;
} dummy_entry_t;

typedef struct timestamp_info {
    uint64_t timestamp;
    uint32_t ring_idx;
} timestamp_info_t;

typedef struct at_entry {
    vs_idx_t vs_idx;
    void *val_addr;
#ifdef MTS_STATS_LATENCY
    uint64_t timestamp;
#endif
} __nvm ____ptr_aligned at_entry_t;

class AddressTable {
    private:
	ts_nvm_root_obj_t *nvm_root_obj;
	int need_recovery;
	size_t num_at_entry;
	at_idx_t at_idx;
	std::list<uint64_t> *free_at_offset_list;
	at_entry_t *at_starting_addr;
	int fd;
	unsigned int at_id;
	bool whole_file_written;

	SpinLock spinlock;
	mutable std::mutex mutex_;

    public:
	AddressTable();
	AddressTable(const char *path, unsigned int at_id);
	~AddressTable();
	uint64_t next_empty_at_offset;

	uintptr_t get_starting_addr();
	at_entry_t *createAddressTable();
	at_entry_t *alloc(Key_t key); 
	at_entry_t *assign(Key_t key);


	bool free(at_entry_t *at_entry);
	bool is_empty(int offset);
	uint64_t get_empty_at_offset();

	size_t get_num_at_entry();
	size_t get_at_entry_num();

	void put_at_entry(at_entry_t *dst, at_entry_t *src);
	at_entry_t *get_at_entry(at_entry_t *at_addr, size_t offset);

	int get_val_pos(at_entry_t *at_entry);
	int get_val_pos(at_entry_t *at_entry, int *cur_vs_id);

	op_entry_t *get_ol_entry(at_entry_t *at_entry);
	void link_to_ol(at_entry_t *at_entry_addr, op_entry_t *oplog_addr);
	void link_to_ol(at_entry_t *at_entry_addr, op_entry_t *oplog_addr, int *past_vs_id, int *past_vs_offset);
	void link_to_vs(void *at_entry_addr, void *vs_addr, size_t vs_chunk_offset, size_t vs_entry_offset);

	void build_bitmap(at_idx_t at_idx);

	bool is_valid(void *at_entry_addr, ValueStorage *vs);
	int get_at_id(at_entry_t *at_entry);
	uintptr_t get_at_offset(at_entry_t *at_entry);
};

#endif /* MTS_ADDRESSTABLE_H */
