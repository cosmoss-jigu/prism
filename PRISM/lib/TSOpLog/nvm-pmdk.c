#ifdef TS_NVM_IS_PMDK
#include <libpmemobj.h>
#include "util.h"
#include "debug.h"
#include "port.h"
#include "nvm.h"

static PMEMobjpool *__g_pop;

ts_nvm_root_obj_t *nvm_load_heap(const char *path, size_t sz, int *is_created)
{
    ts_trace(TS_INFO, "nvm_load_heap\n");
    PMEMoid root;
    ts_nvm_root_obj_t *root_obj;

    /* You should not init twice. */
    ts_assert(__g_pop == NULL);

    /* Open a nvm heap */
    if (access(path, F_OK) != 0) {
	__g_pop =
	    pmemobj_create(path, POBJ_LAYOUT_NAME(nvlog), sz, 0666);
	ts_trace(TS_INFO, "pmemobj_create\n");
	if (unlikely(!__g_pop)) {
	    ts_trace(TS_ERROR, "failed to create pool\n");
	    return NULL;
	}
	*is_created = 1;
    } else {
	if ((__g_pop = pmemobj_open(path, POBJ_LAYOUT_NAME(nvlog))) ==
		NULL) {
	    ts_trace(TS_ERROR,
		    "failed to open the existing pool\n");
	    return NULL;
	}
	else ts_trace(TS_INFO, "pmemobj_open\n");
	*is_created = 0;
    }

    /* Allocate a root in the nvmem pool, here on root_obj
     * will be the entry point to nv pool for all log allocations*/
    root = pmemobj_root(__g_pop, sizeof(ts_nvm_root_obj_t));
    root_obj = pmemobj_direct(root);
    if (!root_obj) {
	return NULL;
    }

    if (*is_created) {
	memset(root_obj, 0, sizeof(*root_obj));
    }

    return root_obj;
}

void nvm_heap_destroy(void)
{
    pmemobj_close(__g_pop);
    __g_pop = NULL;
}

void *nvm_alloc(size_t size)
{
    int ret;
    PMEMoid master_obj;

    //ts_trace(TS_ERROR, "nvm_alloc: %d\lu\n");

    ret = pmemobj_alloc(__g_pop, &master_obj, size, 0, NULL, NULL);
    /* TODO: need to link each object for recovery */
    if (ret) {
	ts_trace(TS_ERROR, "master_obj allocation failed size: %lu\n", size);
	return NULL;
    }
    return pmemobj_direct(master_obj);
}

void *nvm_aligned_alloc(size_t align, size_t len){
    int ret;
    PMEMoid _addr;
    unsigned char *mem, *_new, *end;
    size_t header, footer;
    PMEMobjpool *pop;
    pop = __g_pop;
    if ((align & -align) != align) return EINVAL;
    if (len > SIZE_MAX - align) return ENOMEM;
    if (align <= 4*sizeof(size_t)) {
	ret = pmemobj_alloc(pop, &_addr, len, 0, NULL, NULL);
	if (ret) {
	    perror("[0] nvmm memory allocation failed\n");
	    return -1;
	}
	void *res = pmemobj_direct(_addr);
	if (!res){
	    perror("[1] nvmm memory allocation failed\n");
	    return -1;
	}
	return _new;
	//return 0;
    }
    ret = pmemobj_alloc(pop, &_addr, (len + align-1), 0, NULL, NULL);
    if (ret) {
	perror("[00] nvmm memory allocation failed\n");
	return -1;
    }
    mem = (unsigned char*)(pmemobj_direct(_addr));
    if (!mem){
	perror("[11] nvmm memory allocation failed\n");
	return -1;
    }
    header = ((size_t *)mem)[-1];
    end = mem + (header & -8);
    footer = ((size_t *)end)[-2];
    _new = (unsigned char *)((uintptr_t)mem + align-1 & -align);
    if (!(header & 7)) {
	((size_t *)_new)[-2] = ((size_t *)mem)[-2] + (_new-mem);
	((size_t *)_new)[-1] = ((size_t *)mem)[-1] - (_new-mem);
	void *res = _new;
	return _new;
	//return 0;
    }
    ((size_t *)mem)[-1] = header&7 | _new-mem;
    ((size_t *)_new)[-2] = footer&7 | _new-mem;
    ((size_t *)_new)[-1] = header&7 | end-_new;
    ((size_t *)end)[-2] = footer&7 | end-_new;
    if (_new != mem) nvm_free(mem);
    //*res = _new;
    return _new;
    //return 0;
}
/*
void *nvm_aligned_alloc(size_t alignment, size_t size)
{
    // TODO: need to implement aligned alloc 
    return nvm_alloc(size);
}
*/

void nvm_free(void *ptr)
{
    PMEMoid _ptr;

    _ptr = pmemobj_oid(ptr);
    pmemobj_free(&_ptr);
}

#endif /* TS_NVM_IS_PMDK */
