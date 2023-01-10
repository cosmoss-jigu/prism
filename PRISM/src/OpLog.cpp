#include "OpLog.h"
#include "MTSThread.h"

extern std::vector<ValueStorage *> g_perNumaValueStorage;

OpLog::OpLog(const char *path, int id) {
    nvm_root_obj = nvm_init_heap(path, NVHEAP_POOL_SIZE, &need_recovery);
    nvlog_init(nvm_root_obj);
    oplog_create(NULL, &oplog1, MTS_OPLOG_SIZE, STATUS_NVLOG_NORMAL, 0);
    oplog_create(NULL, &oplog2, MTS_OPLOG_SIZE, STATUS_NVLOG_NORMAL, 1);

    /* Enabling double-buffeing */
    oplog1.ready = true;
    oplog2.ready = false;
    reclaim_lock = false;
    g_oplog_id = id;
    total_ol_write_count = 0;

    working_oplog = &oplog1;
}


OpLog::~OpLog() {
    if(reclaim_lock == true) {
	reclaim_thread->join();
	delete reclaim_thread;
    }
}

op_entry_t *OpLog::enq(Key_t key, Val_t val, int type) {
    op_entry_t *op_entry = put_ol_entry(*working_oplog, key, val);

    if(op_entry == NEED_RECLAIM) {
	ts_trace(TS_INFO, "[ENQ] OPLOG RETURNS 'NEED_RECLAIM'\n");
	if (reclaim_lock == true) {
	    reclaim_thread->join();
	    delete reclaim_thread;
	}

	reclaimed_oplog = working_oplog;
	ts_trace(TS_INFO, "[ENQ] Reclaimed_oplog ID: %d, ready: %d | READY FOR RECLAIMING\n",
		reclaimed_oplog->id, reclaimed_oplog->ready);

	volatile int oplog_id = reclaimed_oplog->id;
	reclaim_thread = new std::thread(&OpLog::reclaim, this, oplog_id);
	pin_thread(reclaim_thread, g_oplog_id, MTS_THREAD_NUM);

	working_oplog = get_another_oplog(reclaimed_oplog);
	op_entry = put_ol_entry(*working_oplog, key, val);
    }

    return op_entry;
}

ts_oplog_t *OpLog::get_another_oplog(ts_oplog_t *oplog) {
    if(oplog->id == 0) {
	return &oplog2;
    } else return &oplog1;
}

op_entry_t *OpLog::put_ol_entry(ts_oplog_t &oplog, Key_t key, Val_t val) {
    op_entry_t *op_entry;
    op_info.key = key;
    op_info.val = val;

    unsigned long entry_size = align_uint_to_cacheline(KV_SIZE);
    unsigned long oplog_index = oplog.tail_cnt - oplog.head_cnt + entry_size;
    op_info.entry_size = entry_size; 
    if (unlikely(oplog_index > MTS_OPLOG_HIGH_MARK)) {
	ts_trace(TS_INFO, "[PUT_OL_ENTRY] returns NEED_RECLAIM oplog_index: %lu HIGH_MARK: %lu\n", oplog_index, MTS_OPLOG_HIGH_MARK);
	return NEED_RECLAIM;
    }

    op_entry = oplog_enq(&oplog, &op_info);

    ts_trace(TS_INFO, "[PUT_OL_ENTRY] oplog->tail_cnt: %lu, oplog->head_cnt: %lu\n", oplog.tail_cnt, oplog.head_cnt);

    assert(op_entry != nullptr);
    return op_entry;
}

op_entry_t *OpLog::oplog_enq(ts_oplog_t *oplog, op_info_t *op_info) {
    /* Reserve a space in oplog */
    op_entry_t *op_entry = (op_entry_t *)nvlog_enq(oplog, op_info->entry_size);

    int dummy_size = sizeof(op_entry->__reserved);
    int val_size = sizeof(op_info->val) + dummy_size;

    op_entry->key = op_info->key;
    op_entry->val = op_info->val;
  
    pmem_persist((void *)&op_entry, op_info->entry_size);
   /* oplog_enq_persist */
    oplog_enq_persist(oplog);

    return op_entry;
}

void OpLog::link_to_at(op_entry_t *op_entry, at_entry_t *at_entry) {
    pmem_memcpy((void *)&op_entry->opa, (void *)&at_entry, sizeof(at_entry), PMEM_F_MEM_NONTEMPORAL);
}

void OpLog::unlink_to_at(at_entry_t *at_entry) {
    //smp_wmb();
    op_entry_t *op_entry = (op_entry_t *)get_untagged_ptr((intptr_t)at_entry->val_addr);
    op_entry->opa = NULL;
    at_entry->val_addr = NULL;
    pmem_persist((void *)&at_entry, sizeof(at_entry_t));
}

void OpLog::reclaim(volatile int oplog_id) {
    /* Init */
    ts_oplog_t *oplog;
    if(oplog_id == 0)
	oplog = &oplog1;
    else oplog = &oplog2;

    oplog->reclaimed = true;
    reclaim_lock = true;
    oplog->ready = false;
    ts_trace(TS_INFO, "[RECLAIM] BEGIN OpLog ID: %d ready: %d\n", oplog->id, oplog->ready);

    op_entry_t *op_entry;
    at_entry_t *at_entry;
    unsigned long old_head_cnt;
    unsigned long reclaimed_ops, reclaimed_bytes;

    /* Ensure all previous writes durable */

    /* PICK VALUESTORAGE */
    std::set<int> written_vs_set;
    ValueStorage *vs;

    vs = pick_valuestorage(oplog_id);
    int vs_id = vs->get_vs_id();
    if(vs_id == 8) 
	ts_trace(TS_ERROR, "VSID 8\n");

    reclaimed_ops = reclaimed_bytes = 0;
    old_head_cnt = oplog->head_cnt;

    while ((op_entry = oplog_peek_head(oplog))) {
	at_entry = (at_entry_t *)op_entry->opa;

	/* validation test */
	if (op_entry == (op_entry_t *)get_untagged_ptr((intptr_t)at_entry->val_addr)) {
	    Key_t key = op_entry->key;
	    Val_t val = op_entry->val;
	    vs->put_vs_entry(g_oplog_id, key, val, at_entry);

	    if(!written_vs_set.count(vs_id))
		written_vs_set.insert(vs_id);
	} else {
	    ts_trace(TS_INFO, "[REC 1. op_entry: %p, at_entry: %p, key: %lu\n", 
		    op_entry, at_entry, op_entry->key);
	}

	/* dequeue an oplog entry for reclaim */
	oplog_deq(oplog);

	reclaimed_ops++;
	ts_trace(TS_INFO, "[REC 5. OPLOG_DEQ] reclaimed_ops: %lu oplog_used(oplog): %lu\n", reclaimed_ops, oplog_used(oplog));
	ts_assert(oplog->head_cnt <= oplog->tail_cnt);

	if(oplog_used(oplog) == MTS_OPLOG_LOW_MARK) {
	    ts_trace(TS_INFO, "[REC 6. VS->F_WRITE] oplog_used(oplog): %lu MTS_OPLOG_LOW_MARK: %u\n", oplog_used(oplog), MTS_OPLOG_LOW_MARK);

	    vs->forced_write_chunk(g_oplog_id);
	    break;
	}

    }

    /* clear written_vs_set */
    written_vs_set.clear();

    /* Persist oplog */
    ts_assert(oplog->head_cnt >= old_head_cnt);
    if (oplog->head_cnt != old_head_cnt) {
	reclaimed_bytes = oplog->head_cnt - old_head_cnt;
	ts_trace(TS_INFO, "Oplog[%p]: %ld bytes reclaimed (h=%ld, t=%ld)\n", oplog, reclaimed_bytes, oplog->head_cnt, oplog->tail_cnt);

	if(MTS_OPLOG_LOW_MARK == 0)
	    oplog->head_cnt = oplog->tail_cnt = 0;

	/* oplog_deq_persist */
	oplog_deq_persist(oplog);
    }

    ts_trace(TS_INFO, "[RECLAIM] END OpLog ID: %d\n", oplog->id);

    oplog->ready = true;
    reclaim_lock = false;
    smp_wmb_tso();
}

op_entry_t *OpLog::nvlog_enq(ts_nvlog_t *nvlog, unsigned int obj_size) {
    /* allocate a buffer for a given size
     * only update nvlog->tail_cnt (do not update nvlog_store->tail_cnt */

    op_entry_t *nvl_entry_hdr;
    unsigned int entry_size;

    /* Make an entry size  */
    entry_size = align_uint_to_cacheline(obj_size);

    ts_assert(KV_SIZE == entry_size);

    if (entry_size > nvlog->log_size) {
	ts_trace(TS_ERROR, "[OVERFLOW_OUT] entry_size > nvlog->log_size)\n");
	goto overflow_out;
    }

    if ((nvlog->tail_cnt + entry_size) - nvlog->head_cnt > nvlog->log_size)
	goto overflow_out;

    nvl_entry_hdr = oplog_at(nvlog, nvlog->tail_cnt);
    nvl_entry_hdr->size = entry_size;

    /* Update nvlog's tail_cnt */
    nvlog->tail_cnt += entry_size;

    return nvl_entry_hdr;

overflow_out:
    /* We assume that the log must be properly reclaimed
     *          * by the upper layer so this should not happen. */
    ts_assert(0 && "fail to allocate nvlog");

    return NULL;
}

void OpLog::oplog_enq_persist(ts_oplog_t *oplog) {
    ts_nvlog_store_t *nvlog_store;

    /* persist tail_cnt */
    oplog->nvlog_store->tail_cnt = oplog->tail_cnt;
}

op_entry_t *OpLog::oplog_at(ts_nvlog_t *nvlog, unsigned long cnt) {
    return (op_entry_t *)&nvlog->buffer[nvlog_index(nvlog, cnt)];
}

unsigned long OpLog::nvlog_index(ts_nvlog_t *nvlog, unsigned long cnt) {
    return cnt & ~nvlog->mask;
}

op_entry_t *OpLog::oplog_deq(ts_oplog_t *oplog) {
    op_entry_t *nvl_entry_hdr;

    /* check if nvlog is empty */
    if (unlikely(oplog->head_cnt == oplog->tail_cnt))
	return NULL;

    /* update head_cnt */
    nvl_entry_hdr = oplog_at(oplog, oplog->head_cnt);
    oplog->head_cnt += nvl_entry_hdr->size;
    return nvl_entry_hdr;
}

void OpLog::oplog_deq_persist(ts_oplog_t *oplog) {
    /* persist head_cnt */
    if (oplog->nvlog_store->head_cnt != oplog->head_cnt) {
	oplog->nvlog_store->head_cnt = oplog->head_cnt;
    }
}

op_entry_t *OpLog::oplog_peek_head(ts_oplog_t *oplog) {
    op_entry_t *nvl_entry_hdr;

    /* check if oplog is empty */
    if (unlikely(oplog->head_cnt == oplog->tail_cnt))
	return NULL;

    /* update head_cnt */
    nvl_entry_hdr = oplog_at(oplog, oplog->head_cnt);

    return nvl_entry_hdr;
}

ValueStorage *OpLog::pick_valuestorage(int oplog_id) {
    std::random_device ol_rd;
    std::mt19937 ol_gen(ol_rd());
    std::uniform_int_distribution<> dist(0, MTS_VS_NUM-1);
    int vs_id = dist(ol_gen);
    int vs_id1 = dist(ol_gen);
    int vs_id2 = dist(ol_gen);

    ValueStorage *vs;

    ValueStorage *vs1 = g_perNumaValueStorage[vs_id1];
    ValueStorage *vs2 = g_perNumaValueStorage[vs_id2];

    if(vs1->need_gc() && vs2->need_gc()) {
	vs_id = (vs_id1 < vs_id2) ? vs_id1 : vs_id2;
    } else if (vs1->get_used_chunk_num() < vs2->get_used_chunk_num()) {
	vs_id = vs_id1;
    } else
	vs_id = vs_id2;

    ts_trace(TS_INFO, "[PICK_VS] OPLOG_ID: %d VS_ID: %d(%d), VS_ID1: %d(%d), VS_ID2: %d(%d)\n", 
	    oplog_id, vs_id, vs->get_used_chunk_num(),
	    vs_id1, vs1->get_used_chunk_num(), vs_id2, vs2->get_used_chunk_num());

    vs = g_perNumaValueStorage[vs_id];

    while(true) {
	if(!vs->is_writing) {
	    ts_trace(TS_INFO, "[PICK_VS] OPLOG_ID: %d VS_ID: %d %d / %lu\n", oplog_id, vs_id, vs->get_used_chunk_num(), MTS_VS_HIGH_MARK);
	    break;
	}

	vs_id = dist(ol_gen);
	vs = g_perNumaValueStorage[vs_id];
	ts_trace(TS_INFO, "[TRY_PICK_VS] OPLOG_ID: %d  VS_ID: %d %d / %lu\n", oplog_id, vs_id, vs->get_used_chunk_num(), MTS_VS_HIGH_MARK);
    }

    return vs;
}
