#include "LRUList.h"

LRUList::LRUList(unsigned int list_type) {
    dcache = (cache_t *)malloc(sizeof(cache_t));
    if(list_type == ACTIVE_LIST)
	dcache->max_size = MTS_ACTIVE_LIST_SIZE / sizeof(dc_entry_t);
    else dcache->max_size = MTS_INACTIVE_LIST_SIZE / sizeof(dc_entry_t);
    dcache->cur_size = 0;
    dcache->list_type = list_type; 
    dcache->head = NULL;
    dcache->tail = NULL;
}

LRUList::~LRUList() {
    dc_entry_t *cur_entry = dcache->head;
    while(get_cur_size()) {
	dc_entry_t *next_entry = cur_entry->next;
	remove_entry(cur_entry);
	free_entry(cur_entry);
	cur_entry = next_entry;
    }

    free(dcache);
}

int LRUList::get_cur_size() {
    return dcache->cur_size;
}

int LRUList::get_max_size() {
    return dcache->max_size;
}

dc_entry *LRUList::alloc_entry(cq_entry_t *cq_entry) {
    dc_entry *dc_entry = (dc_entry_t *)malloc(sizeof(dc_entry_t));

    if(dc_entry == NULL) {
	ts_trace(TS_INFO, "Failed to allocate dc_entry memory LRUList::alloc()\n");
	exit(EXIT_FAILURE);
    }

    dc_entry->at_entry = cq_entry->at_entry;
    dc_entry->key = cq_entry->key;
    dc_entry->val = cq_entry->val;
    dc_entry->list_type = dcache->list_type;

    dc_entry->prev = NULL;
    dc_entry->next = NULL;
    dc_entry->s_prev = NULL;
    dc_entry->s_next = NULL;

    ts_trace(TS_INFO, "[alloc_entry] dc_entry: %p val: %lu\n", dc_entry, dc_entry->val);

    return dc_entry;
}

bool LRUList::chain_has_active_entry(dc_entry_t *dc_entry) {
    if(dc_entry->s_prev)
	return true;
    if(dc_entry->s_next)
	return true;
    else 
	return false;
}

void LRUList::free_entry(dc_entry_t *dc_entry) {
    ts_trace(TS_INFO, "[free_entry] at_entry: %p dc_entry: %p val: %lu\n",
	    dc_entry->at_entry, dc_entry, dc_entry->val);

    /* uncoupling scanned items */
    if(dc_entry->s_prev)
	dc_entry->s_prev->s_next = dc_entry->s_next;
    if(dc_entry->s_next)
	dc_entry->s_next->s_prev = dc_entry->s_prev;

    free(dc_entry);
    ts_trace(TS_INFO, "[free_entry] after free() | dc_entry: %p\n", dc_entry);

}

void LRUList::insert_head(dc_entry_t *dc_entry) {
    if(dcache->head == NULL) {
	dcache->head = dcache->tail = dc_entry;
	dc_entry->prev = dc_entry->next = NULL;
    } else {
	dcache->head->prev = dc_entry;
	dc_entry->next = dcache->head;
	dc_entry->prev = NULL;
	dcache->head = dc_entry;
    }

    dc_entry->list_type = dcache->list_type;
    dcache->cur_size++;
}

dc_entry_t *LRUList::iter_dcache() {
    dc_entry_t *temp = dcache->head;
    return temp;
    while(temp) {
	ts_trace(TS_INFO, "[iter_dcache] %d dc_entry: %p(%lu)\n", temp->list_type, temp, temp->val);
	temp = temp->next;
    }
}

void LRUList::move_to_head(dc_entry_t *dc_entry) {
    if(dc_entry != dcache->head) {
	if(dc_entry == dcache->tail) {
	    dcache->tail = dc_entry->prev;
	    dcache->tail->next = NULL;
	} else {
	    dc_entry->prev->next = dc_entry->next;
	    dc_entry->next->prev = dc_entry->prev;
	}
	dc_entry->next = dcache->head;
	dcache->head->prev = dc_entry;
	dc_entry->prev = NULL;
	dcache->head = dc_entry;
    }
}

void LRUList::move_to_tail(dc_entry_t *dc_entry) {
    if(dcache->tail != dc_entry) {
	if(dc_entry == dcache->head) {
	    dcache->head = dc_entry->next;
	    dcache->head->prev = NULL;
	} else {
	    dc_entry->next->prev = dc_entry->prev;
	    dc_entry->prev->next = dc_entry->next;
	}
	dc_entry->prev = dcache->tail;
	dcache->tail->next = dc_entry;
	dc_entry->next = NULL;
	dcache->tail = dc_entry;
    }
}

dc_entry_t *LRUList::get_tail() {
    return dcache->tail;
}

dc_entry_t *LRUList::remove_tail() {
    dc_entry_t *old_tail = dcache->tail;

    if(old_tail->prev == NULL)
	ts_trace(TS_INFO, "[remove_tail] old->prev is null\n");
    dcache->tail = old_tail->prev;
    dcache->tail->next = NULL;

    dcache->cur_size--;

    return old_tail;
}

dc_entry_t *LRUList::remove_entry(dc_entry_t *dc_entry) {
    if(dcache->head == dcache->tail) {
	dcache->head = dcache->tail = NULL;
    } else if(dcache->head == dc_entry) {
	dcache->head = dc_entry->next;
	dcache->head->prev = NULL;
    } else if(dcache->tail == dc_entry) {
	dcache->tail = dc_entry->prev;
	dcache->tail->next = NULL;
    } else {
	dc_entry->prev->next = dc_entry->next;
	dc_entry->next->prev = dc_entry->prev;
    }

    at_entry_t *at_entry = dc_entry->at_entry;
    op_entry_t *past_vs_addr = (op_entry_t *)at_entry->val_addr;
    op_entry_t *new_vs_addr = (op_entry_t *)put_tagged_ptr((intptr_t)at_entry->val_addr, VALUESTORAGE_VAL);

    if(get_tag((intptr_t)past_vs_addr) == DCACHE_VAL) {
	smp_cas(&at_entry->val_addr, past_vs_addr, new_vs_addr);
	pmem_persist((void *)&at_entry, sizeof(at_entry_t));
    }

    dcache->cur_size--;

    return dc_entry;
}
