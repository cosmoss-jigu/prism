#include "AddressTable.h"

#define MAX_AT_ENTRY_NUM (MTS_AT_SIZE / sizeof(at_entry_t))

void __attribute__((optimize("O0"))) prefault_p(void *addr, size_t size) {
    unsigned char c[64];
    for (size_t i = 0; i < size; i += sizeof(at_entry_t)) {
	memcpy(&c, (char*)addr+i, sizeof(at_entry_t));
	memset((char*)addr+i, 0x00, sizeof(at_entry_t));
    }
}

AddressTable::AddressTable() {}
AddressTable::~AddressTable() {}

AddressTable::AddressTable(const char *path, unsigned int at_num) {
    /* Creat PMEM FILE and memory mapt it */   
    void *mem;
    int ret;

    void *pmem_addr;
    size_t mapped_len;
    int is_pmem;

    /* Create a pmem file and memory map it */
    if ((pmem_addr = pmem_map_file(path, MTS_AT_SIZE, PMEM_FILE_CREATE, 
		    0666, &mapped_len, &is_pmem)) == NULL) {
	ts_trace(TS_ERROR, "pmem_map_file\n");
	exit(EXIT_FAILURE);
    }

    /* Assign mapped pmem_addr */
    at_starting_addr = (at_entry_t *)pmem_addr;

    if(at_starting_addr == MAP_FAILED) {
	ts_trace(TS_ERROR, "mmap failed\n");
	exit(EXIT_FAILURE);
    }

    free_at_offset_list = new std::list<uint64_t>;
    whole_file_written = false;

    at_id = at_num;
    next_empty_at_offset = 0;

    ts_trace(TS_INFO, "[AT_INIT] id: %d at_starting_addr: %p\n", at_num, at_starting_addr);
}

void AddressTable::put_at_entry(at_entry_t *dst, at_entry_t *src) {
    pmem_memcpy((void *)dst, (void *)src, sizeof(*dst), PMEM_F_MEM_NONTEMPORAL);
}

size_t AddressTable::get_at_entry_num() {
    size_t at_size = 0;
    size_t num_at_entry = 0;

    if(whole_file_written)
	return (MTS_AT_ENTRY_NUM - free_at_offset_list->size());
    else return next_empty_at_offset;
}

bool AddressTable::is_empty(int offset) {
	at_entry_t *at_entry = (at_entry_t *)&at_starting_addr[offset];

	int tag = get_tag((intptr_t)at_entry->val_addr);

	if(tag == CLEAN_ENTRY)
	    return true;
	else return false;
}

uint64_t AddressTable::get_empty_at_offset() {
    uint64_t at_offset;

    if(!whole_file_written) {
	at_offset = next_empty_at_offset++;

	while(is_empty(at_offset) == false) {
	    at_offset = next_empty_at_offset++;
	}

	if(next_empty_at_offset == MTS_AT_ENTRY_NUM) 
	    whole_file_written = true;
    } else {
	ts_trace(TS_ERROR, "[AT_get_empty_at_offset] %d\n", free_at_offset_list->size());
	assert(free_at_offset_list->size() > 0);
	at_offset = free_at_offset_list->front();
	free_at_offset_list->pop_front();
    }

    return at_offset;
}

at_entry_t *AddressTable::alloc(Key_t key) {
    /* step 1. find the empty_idx */
    at_entry_t *at_entry = new at_entry_t;
    at_entry->val_addr = NULL;
    at_entry->vs_idx = {-1, -1};

#ifdef MTS_STATS_LATENCY
    at_entry->timestamp = 0;
#endif

    return at_entry;
}

at_entry_t *AddressTable::assign(Key_t key) {
    at_entry_t *at_entry = alloc(key);

    size_t offset = get_empty_at_offset();
    if (unlikely(offset >= (MTS_AT_SIZE/MTS_AT_ENTRY_SIZE))) {
	ts_trace(TS_ERROR, "[AT_ASSIGN] Fail to assign a addresstable entry\n");
	spinlock.unlock();
	exit(EXIT_FAILURE);
    }

    ts_trace(TS_INFO, "[AT_ASSIGN] (at_entry_t *)mem: %p offset: %d\n", 
	    (at_entry_t *)&at_starting_addr[offset], offset);
    put_at_entry((at_entry_t *)&at_starting_addr[offset], at_entry);

    return (at_entry_t *)&at_starting_addr[offset];
}

uintptr_t AddressTable::get_starting_addr() {
    return (uintptr_t)&at_starting_addr[0];
}

bool AddressTable::free(at_entry_t *at_entry) {
    op_entry_t *new_vs_addr = (op_entry_t *)put_tagged_ptr((intptr_t)at_entry->val_addr, CLEAN_ENTRY);
    at_entry->val_addr = new_vs_addr;
    at_entry->vs_idx = {-1, -1};

    pmem_persist((void *)&at_entry, sizeof(at_entry_t));

    /* management in free list */
    uintptr_t at_offset = get_at_offset(at_entry);
    free_at_offset_list->push_back(at_offset);

    return true;
}

op_entry_t *AddressTable::get_ol_entry(at_entry_t *at_entry) {
    op_entry_t *op_entry = (op_entry_t *)at_entry->val_addr;
    return op_entry;
}

void AddressTable::link_to_ol(at_entry_t *at_entry, op_entry_t *op_entry) {
    intptr_t tagged_entry = put_tagged_ptr((intptr_t)op_entry, OPLOG_VAL);
    pmem_memcpy((void *)&at_entry->val_addr, (void *)&tagged_entry, sizeof(op_entry), PMEM_F_MEM_NONTEMPORAL);
    _mm_sfence();
}

void AddressTable::link_to_ol(at_entry_t *at_entry, op_entry_t *op_entry, int *past_vs_id, int *past_vs_offset) {
    *past_vs_id = at_entry->vs_idx.vs_id;
    *past_vs_offset = at_entry->vs_idx.vs_offset;
    vs_idx_t new_vs_idx = {-1, -1};

    at_entry_t *tagged_entry = new at_entry_t;
    tagged_entry->val_addr = (op_entry_t *)put_tagged_ptr((intptr_t)op_entry, OPLOG_VAL);
    tagged_entry->vs_idx = new_vs_idx;
    pmem_memcpy((void *)at_entry, (void *)tagged_entry, sizeof(at_entry_t), PMEM_F_MEM_NONTEMPORAL);
    _mm_sfence();
}

at_entry_t *AddressTable::get_at_entry(at_entry_t *mem, size_t offset) {
    return (at_entry_t *)&mem[offset];
}

int AddressTable::get_at_id(at_entry_t *at_entry) {
    if(((uintptr_t)at_entry >= (uintptr_t)&at_starting_addr[0]) &&
	((uintptr_t)at_entry <= (uintptr_t)&at_starting_addr[MTS_AT_ENTRY_NUM]))
    return at_id;
    else return -1;
}

uintptr_t AddressTable::get_at_offset(at_entry_t *at_entry) {
    uintptr_t interval = (uintptr_t)&at_starting_addr[1] - (uintptr_t)&at_starting_addr[0];
    uintptr_t at_offset = (uintptr_t)at_entry - (uintptr_t)&at_starting_addr[0];
    at_offset = at_offset / interval;
    return at_offset;
}
