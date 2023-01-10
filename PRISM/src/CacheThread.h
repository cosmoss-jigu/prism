#ifndef MTS_CACHETHREAD_H
#define MTS_CACHETHREAD_H

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
#include <shared_mutex>
#include "LRUList.h"
#include "MTSImpl.h"

class LRUList;

typedef struct cq_entry cq_entry_t;

class CacheThread {
    private:
	LRUList *active_list;
	LRUList *inactive_list;

    public:
	mutable std::shared_mutex s_mutex_;
	CacheThread();
	~CacheThread();

	LRUList *createLRUList(unsigned int list_type);
	void cacheOperation(std::vector<cq_entry_t *> *cq_entry_vec);
	void freeOperation(at_entry_t *at_entry);

	bool is_cached(cq_entry_t *cq_entry);

	void link_to_at(dc_entry_t *dc_entry);
	ValueStorage *pick_valuestorage();
	void evict_entry();
	Val_t get_val(at_entry_t *at_entry);
};


#endif /* MTS_CACHETHREAD_H */
