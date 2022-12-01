#include "CacheThread.h"
extern std::vector<ValueStorage *> g_perNumaValueStorage;
std::random_device ct_rd;
std::mt19937 ct_gen(ct_rd());

CacheThread::CacheThread() {
    active_list = createLRUList(ACTIVE_LIST);
    inactive_list = createLRUList(INACTIVE_LIST);
}

CacheThread::~CacheThread() {
    delete active_list;
    delete inactive_list;
}

LRUList *CacheThread::createLRUList(unsigned int list_type) {
    return new LRUList(list_type);
}

int which_list(dc_entry_t *dc_entry) {
    if(dc_entry == NULL)
	return NONE;

    ts_trace(TS_INFO, "[which_list] dc_entry: %p(%lu)\n", dc_entry, dc_entry->val);

    /* ACTIVE_LIST or INACTIVE_LIST */
    return dc_entry->list_type;
}

void CacheThread::link_to_at(dc_entry_t *dc_entry) {
    at_entry_t *at_entry = dc_entry->at_entry;
    dc_entry_t *past_vs_addr = (dc_entry_t *)at_entry->val_addr;
    
    if((at_entry->vs_idx.vs_id < 0) && (get_tag((intptr_t)past_vs_addr) == OPLOG_VAL))
	return;

    at_entry_t *tagged_entry = new at_entry_t;
    tagged_entry->val_addr = (dc_entry_t *)put_tagged_ptr((intptr_t)dc_entry, DCACHE_VAL);
    smp_cas(&at_entry->val_addr, past_vs_addr, tagged_entry->val_addr);
}

ValueStorage *CacheThread::pick_valuestorage() {
    ValueStorage *vs;
    int vs_id;

    std::uniform_int_distribution<> dist(0, MTS_VS_NUM-1);
    int vs_id1 = dist(ct_gen);
    int vs_id2 = dist(ct_gen);
    ts_trace(TS_INFO, "[CACHE_PICK_VS] VS_ID1: %d, VS_ID2: %d\n", vs_id1, vs_id2);
    ValueStorage *vs1 = g_perNumaValueStorage[vs_id1];
    ValueStorage *vs2 = g_perNumaValueStorage[vs_id2];

    if (vs1->get_used_chunk_num() < vs2->get_used_chunk_num()) {
	vs_id = vs_id1;
	vs = vs1;
    } else {
	vs_id = vs_id2;
	vs = vs2;
    }
    while(true) {
	if(!vs->is_writing) {
	    ts_trace(TS_WARNING, "[CACHE_PICK_VS] VS_ID: %d %d / %lu\n", vs_id, vs->get_used_chunk_num(), MTS_VS_HIGH_MARK);
	    return vs;
	}
	vs_id = dist(ct_gen);
	vs = g_perNumaValueStorage[vs_id];
	ts_trace(TS_WARNING, "[CACHE_TRY_PICK_VS] VS_ID: %d %d / %lu\n", vs_id, vs->get_used_chunk_num(), MTS_VS_HIGH_MARK);
    }
}

void CacheThread::evict_entry() {
    bool ops;
    bool has_active_list_entry;
    bool written_active_list_entry = false;
    ValueStorage *vs = NULL;
    std::set<int> written_vs_set;

    std::uniform_int_distribution<> dist(0, MTS_OPLOG_NUM-1);
    int g_oplog_id = dist(ct_gen);

    for(int i = 0; i < MTS_RECLAIM_PAGES_NUM; i++) {
	dc_entry_t *removed_entry = inactive_list->get_tail();
	dc_entry_t *next_removed_entry;

	if(removed_entry != (dc_entry_t *)get_untagged_ptr((intptr_t)removed_entry->at_entry->val_addr)) {
	    inactive_list->remove_entry(removed_entry);
	    inactive_list->free_entry(removed_entry);
	    i++;
	    continue;
	}

	if(removed_entry->s_prev == NULL)
	    ops = CT_LOOKUP;
	else ops = CT_SCAN;

	if(ops == CT_LOOKUP) {
	    inactive_list->remove_entry(removed_entry);
	    inactive_list->free_entry(removed_entry);
	    i++;
	}
	else if(ops == CT_SCAN) {
	    has_active_list_entry = false;

	    /* moved the first of chained entries for scan */
	    while(removed_entry) {
		if(removed_entry->s_prev == NULL)
		    break;
		else {
		    removed_entry = removed_entry->s_prev;
		    if(removed_entry->list_type == ACTIVE_LIST)
			has_active_list_entry = true;
		}
	    }

	    if(has_active_list_entry == true) {
		written_active_list_entry = true;
		vs = pick_valuestorage();
	    }

	    while(removed_entry) {
		ts_trace(TS_INFO,"[evict_entry] removed_entry: %p val: %lu\n",
			removed_entry, removed_entry->val);

		if(has_active_list_entry == true) {
		    Key_t key = removed_entry->key;
		    Val_t val = removed_entry->val;
		    at_entry_t *at_entry = removed_entry->at_entry;
		    ts_trace(TS_INFO, "[evict_entry] vs->put_vs_entry val: %lu at_entry: %p\n", val, at_entry);

		    int vs_id = vs->get_vs_id();
		    vs->put_vs_entry(g_oplog_id, key, val, at_entry);

		    if(!written_vs_set.count(vs_id))
			written_vs_set.insert(vs_id);
		}

		next_removed_entry = removed_entry->s_next;
		ts_trace(TS_INFO, "[evict_entry] next_removed_entry: %p removed_entry: %p\n", next_removed_entry, removed_entry);

		if(removed_entry->list_type == INACTIVE_LIST) {
		    inactive_list->remove_entry(removed_entry);
		    inactive_list->free_entry(removed_entry);
		}
		removed_entry = next_removed_entry;

		ts_trace(TS_INFO, "[evict_entry] inactive_list->get_cur_size(): %d\n", inactive_list->get_cur_size());

		if(inactive_list->get_cur_size() == 0)
		    break;
		i++;
	    }

	    if(inactive_list->get_cur_size() == 0)
		break;

	    if(written_active_list_entry) {
		if(vs != NULL) { 
		    vs->forced_write_chunk(g_oplog_id);
		}
	    }
	}

	ts_trace(TS_INFO, "[evict_entry] finish evict()\n");
    }
}

void CacheThread::freeOperation(at_entry_t *at_entry) {
    auto dc_entry = (dc_entry_t *)at_entry->val_addr;
    if(dc_entry != NULL) {
	ts_trace(TS_INFO, "[freeOperation] dc_entry: %p(%lu)\n", dc_entry, dc_entry->val);

	if(smp_cas(&at_entry->val_addr, dc_entry, nullptr)) {
	    if(dc_entry->list_type == ACTIVE_LIST) {
		active_list->remove_entry(dc_entry);
		active_list->free_entry(dc_entry);
	    }
	    else {
		inactive_list->remove_entry(dc_entry);
		inactive_list->free_entry(dc_entry);
	    }

	}
    }
}

void CacheThread::cacheOperation(std::vector<cq_entry_t *> *cq_entry_vec) {
    /*
     * step 1. lookup or scan
     * step 2. it is cacahed already or not
     */
    cq_entry_t *cq_entry;
    dc_entry_t *dc_entry;
    dc_entry_t *s_dc_entry = NULL; /* for scan chain */
    bool scan_ops;

    if(cq_entry_vec->front()->ops == CT_LOOKUP) scan_ops = false;
    else if(cq_entry_vec->front()->ops == CT_SCAN) scan_ops = true;
    else {
	ts_trace(TS_ERROR, "cacheOperation | Wrong ops... \n");
    }

    std::vector<cq_entry_t *>::iterator iter;
    for(iter = cq_entry_vec->begin(); iter != cq_entry_vec->end(); iter++) {
	cq_entry = *iter;
	ts_trace(TS_INFO, "[cacheOperation] cq_entry: %p cq_entry->at_entry: %p cq_entry->val: %lu\n",
		cq_entry, cq_entry->at_entry, cq_entry->val);

	/* concurrent update or not */
	at_entry_t *at_entry = cq_entry->at_entry;

	if(at_entry == NULL) {
	    continue;
	}

	/* update during scanning keys */
	if((at_entry->vs_idx.vs_id < 0) && (get_tag((intptr_t)at_entry->val_addr) == OPLOG_VAL)) {
	    continue;
	}

	if(get_tag((intptr_t)at_entry->val_addr) == DCACHE_VAL) {
	    dc_entry = (dc_entry_t *)get_untagged_ptr((intptr_t)at_entry->val_addr);

	    unsigned int cur_list_type = which_list(dc_entry);

	    /* Case 1. hit entry of INACTIVE_LIST */
	    if(cur_list_type == INACTIVE_LIST) { 
		ts_trace(TS_INFO, "[cacheOperation] Hit on INACTIVE_LIST | dc_entry: %p val: %lu\n", dc_entry, dc_entry->val);

		inactive_list->remove_entry(dc_entry);
		active_list->insert_head(dc_entry);

		/* chainning scanned items */
		if(scan_ops == true) {
		    if(s_dc_entry != NULL) {
			if(s_dc_entry->s_prev != NULL)
			    s_dc_entry->s_prev = dc_entry;
			if(dc_entry->s_next != NULL)
			    dc_entry->s_next = s_dc_entry;
			ts_trace(TS_INFO, "[cacheOperation-scan] dc_entry: %p val: %lu s_dc_entry: %p val: %lu\n",
				dc_entry, dc_entry->val, s_dc_entry, s_dc_entry->val);
		    }
		    s_dc_entry = dc_entry;
		}

		link_to_at(dc_entry);

		/* copy the entry from active_list to inactive_list */
		if(active_list->get_cur_size() > active_list->get_max_size()) { 
		    dc_entry_t *moved_entry = active_list->remove_tail();
		    inactive_list->insert_head(moved_entry);
		    ts_trace(TS_INFO, "[cacheOperation] MOVE from ACTIVE to INACTIVE | moved_entry: %p\n", moved_entry);
		    /* remove the entry from inactive_list */
		    if(inactive_list->get_cur_size() > inactive_list->get_max_size()) {
			evict_entry();
		    }

		}
	    }

	    /* Case 2. hit entry of ACTIVE_LIST */
	    else if(cur_list_type == ACTIVE_LIST) { 
		ts_trace(TS_INFO, "[cacheOperation] Hit on ACTIVE_LIST | dc_entry: %p val: %lu\n", dc_entry, dc_entry->val);
		active_list->move_to_head(dc_entry);

		/* chainning scanned items */
		if(scan_ops == true) {
		    if(s_dc_entry != NULL) {
			if(s_dc_entry->s_prev != NULL)
			    s_dc_entry->s_prev = dc_entry;
			if(dc_entry->s_next != NULL)
			    dc_entry->s_next = s_dc_entry;
			ts_trace(TS_INFO, "[cacheOperation-scan] dc_entry: %p val: %lu s_dc_entry: %p val: %lu\n",
				dc_entry, dc_entry->val, s_dc_entry, s_dc_entry->val);
		    }
		    s_dc_entry = dc_entry;
		}

		link_to_at(dc_entry);
	    }

	}

	/* Case 3. first access */
	else {
	    dc_entry = inactive_list->alloc_entry(cq_entry);
	    ts_trace(TS_INFO, "[cacheOperation] FIRST ACCESS | dc_entry: %p at_entry: %p val: %lu\n", 
		    dc_entry, dc_entry->at_entry, dc_entry->val);

	    inactive_list->insert_head(dc_entry);

	    /* chainning scanned items */
	    if(scan_ops == true) {
		if(s_dc_entry != NULL) {
		    if(s_dc_entry->s_prev != NULL)
			s_dc_entry->s_prev = dc_entry;
		    if(dc_entry->s_next != NULL)
			dc_entry->s_next = s_dc_entry;
		    ts_trace(TS_INFO, "[cacheOperation-scan] dc_entry: %p val: %lu s_dc_entry: %p val: %lu\n",
			    dc_entry, dc_entry->val, s_dc_entry, s_dc_entry->val);
		}
		s_dc_entry = dc_entry;
	    }

	    link_to_at(dc_entry);

	    if(inactive_list->get_cur_size() > inactive_list->get_max_size()) {
		ts_trace(TS_INFO, "[cacheOperation] inactive_list->get_cur_size(): %d inactive_list->get_max_size(): %d\n",
			inactive_list->get_cur_size(), inactive_list->get_max_size());
		evict_entry();
	    }
	}
    }
    smp_wmb_tso();
}
