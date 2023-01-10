#include "AIO.h"
#define MTS_STOP_TIMER(start, end)  \
{                                   \
    end = read_tscp() - start;      \
}

int apply_ops(aio_struct_t *l, aio_thread_state_t *st_thread, at_entry_t *(*sfunc)(at_entry_t *, std::vector<at_entry_t *> *),
	at_entry_t *at_entry, ValueStorage *valuestorage, int ring_idx) {

    std::vector<at_entry_t *> *dst_at_entry_vec = valuestorage->dst_at_entry_vec[ring_idx];
    std::vector<at_entry_t *> *src_at_entry_vec = valuestorage->src_at_entry_vec[ring_idx];

    volatile aio_node_t *p; 
    volatile aio_node_t *cur;
    aio_node_t *next_node, *tmp_next;
    int help_bound = R_QD;
    int counter = 0;

    next_node = st_thread->next;
    next_node->next = nullptr;
    next_node->locked = true;
    next_node->completed = false;

    cur = (aio_node_t *)SWAP(&l->tail, next_node);
    cur->tmp = at_entry;
    cur->next = (aio_node_t *)next_node;
    st_thread->next = (aio_node_t *)cur;

    while (cur->locked) { // spinning
	sched_yield();
    }   
    if (cur->completed) { // Follower
	return counter;
    }
    p = cur; // Leader

    do {
	while((p->next != nullptr) && (counter < help_bound)) {
	    StorePrefetch(p->next);
	    counter++;
	    ts_trace(TS_INFO, "apply_ops | %p \n", p->tmp);
	    tmp_next = p->next;
	    p->arg_ret = sfunc(p->tmp, src_at_entry_vec);
	    NonTSOFence();
	    p->completed = true;
	    NonTSOFence();
	    p->locked = false;
	    p = tmp_next;
	}
	FullFence();

} while(valuestorage->pending_ios[ring_idx]);

valuestorage->get_val_ccsync(src_at_entry_vec, ring_idx);

NonTSOFence();
p->locked = false; // Unlock the next one
StoreFence();

return counter;
}

void aio_struct_init(aio_struct_t *l) {
    l->nodes = NULL;
    l->tail = (aio_node_t *)get_aligned_memory(L1_CACHE_BYTES, sizeof(aio_node_t));

    l->tail->next = nullptr;
    l->tail->locked = false;
    l->tail->completed = false;

    StoreFence();
}

void aio_thread_state_init(aio_thread_state_t *st_thread) {
    st_thread->is_working = false;
    st_thread->next = (aio_node_t *)get_aligned_memory(L1_CACHE_BYTES, sizeof(aio_struct_t));
}
