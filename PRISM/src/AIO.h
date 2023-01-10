#ifndef MTS_AIO_H
#define MTS_AIO_H

#include "MTSImpl.h"
#include "primitives.h"

#define CACHE_ALIGN   __attribute__((aligned(L1_CACHE_BYTES)))
#define PAD_CACHE(A) ((L1_CACHE_BYTES - (A % L1_CACHE_BYTES)) / sizeof(char))

typedef struct half_aio_node {
    struct aio_node *next;
    at_entry_t *arg_ret;
    int locked;
    int completed;
} half_aio_node_t;
    

typedef struct aio_node {
    struct aio_node *next;
    at_entry_t *arg_ret;
    at_entry_t *tmp;
    int locked;
    int completed;
    char align[PAD_CACHE(sizeof(half_aio_node))];
} aio_node_t;

typedef struct aio_thread_state {
    aio_node *next;
    bool is_working;
    int toggle;
} aio_thread_state_t;

typedef struct aio_struct {
    volatile aio_node *tail CACHE_ALIGN;
    volatile aio_node *nodes CACHE_ALIGN;
    std::atomic<bool> is_working;
#ifdef MTS_DEBUG
    volatile uint64_t counter CACHE_ALIGN;
    volatile int rounds;
#endif
} aio_struct_t;

inline void *get_aligned_memory(size_t align, size_t size) {
    void *p;

    p = numa_alloc_local(size + align);
    long plong = (long)p;
    plong += align;
    plong &= ~(align - 1);
    p = (void *)plong;

    if (p == nullptr) {
	perror("memory allocation fail");
	exit(EXIT_FAILURE);
    } else
	return p;
}

int apply_ops(aio_struct_t *l, aio_thread_state_t *st_thread,
	at_entry_t *(*sfunc)(at_entry_t *, std::vector<at_entry_t *> *), at_entry_t *at_entry, ValueStorage *valuestorage, int ring_idx);
int apply_ops(aio_struct_t *l, aio_thread_state_t *st_thread,
	at_entry_t *(*sfunc)(Key_t , KeyIndex *, std::vector<at_entry_t *> *), 
	Key_t key, KeyIndex *keyindex, std::vector<at_entry_t *> *at_entry_vec);
void aio_struct_init(aio_struct_t *l);
void aio_thread_state_init(aio_thread_state_t *st_thread);

inline static at_entry_t *batching_io(at_entry_t *at_entry, std::vector<at_entry_t *> *cur_at_entry_vec) {
    cur_at_entry_vec->push_back(at_entry);
    return at_entry; 
}

#endif
