#ifndef _NVMEM_H
#define _NVMEM_H

#include "timestone_i.h"

#define UNIT_SIZE L1_CACHE_BYTES
#define NT_SIZE UNIT_SIZE/sizeof(unsigned long int)

#ifdef __cplusplus
extern "C" {
#endif

    unsigned long nvm_get_gen_id(void);
    ts_nvm_root_obj_t *nvm_load_heap(const char *, size_t, int *);
    ts_nvm_root_obj_t *nvm_init_heap(const char *, size_t, int *);
    void nvm_heap_destroy(void);
    void *nvm_alloc(size_t size);
    void *nvm_aligned_alloc(size_t alignment, size_t size);
    void nvm_free(void *ptr);

    static inline void flush_to_nvm(void *dst, unsigned int size)
    {
	unsigned int i;

	for (i = 0; i < size; i += TS_CACHE_LINE_SIZE) {
	    clwb(((char *)dst) + i);

	}
    }

    static inline void memcpy_to_nvm(void *dst, void *src, unsigned int size)
    {
	//ts_assert(dst && src && size);
	memcpy(dst, src, size);
	flush_to_nvm(dst, size);
    }

    struct mem_data {
	char buff[UNIT_SIZE];
    };

    static inline void nvm_read(volatile struct mem_data *dst, volatile struct mem_data *src)
    {
	memcpy((void *)dst, (void *)src, sizeof(*dst));
    }

    static inline void nvm_write(volatile struct mem_data *dst, volatile struct mem_data *src, unsigned int size)
    {
	memcpy((void *)dst, (void *)src, sizeof(*dst));
    }

#ifdef __cplusplus
}
#endif
#endif
