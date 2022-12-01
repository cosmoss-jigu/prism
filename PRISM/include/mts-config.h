#ifndef _MTS_CONFIG_H
#define _MTS_CONFIG_H
#include "arch.h"

#define MTS_THREAD_NUM 32
#define KV_SIZE 1024UL
#define SECTOR_SIZE 512UL 

/* KeyIndex */
#define MTS_KEYINDEX_NUM 1

/* HSIT */
#define MTS_AT_NUM MTS_THREAD_NUM
/* Set to the same value as the number of prism thread*/
#define MTS_AT_PATH "/mnt/pmem"
#define MTS_AT_G_SIZE (1024UL * 1024UL * 1024UL * 1UL * MTS_AT_NUM)
#define MTS_AT_SIZE (MTS_AT_G_SIZE / MTS_AT_NUM)
#define MTS_AT_ENTRY_SIZE sizeof(at_entry_t)
#define MTS_AT_ENTRY_NUM (MTS_AT_SIZE / MTS_AT_ENTRY_SIZE)

/* PWB */
#define MTS_OPLOG_NUM MTS_THREAD_NUM
#define NVHEAP_POOL_PATH "/mnt/pmem"
#define NVHEAP_POOL_SIZE (MTS_OPLOG_G_SIZE / MTS_THREAD_NUM * 4UL)
#define MTS_OPLOG_G_SIZE (16UL * 1024UL * 1024UL * 1024UL)
#define MTS_OPLOG_SIZE (MTS_OPLOG_G_SIZE / MTS_OPLOG_NUM / 2UL)
/* for double-buffering, divided by 2 */
#define MTS_OPLOG_HIGH_MARK (MTS_OPLOG_SIZE)
/* bytes */
#define MTS_OPLOG_LOW_MARK 0 

/* Value Storage */
#define MTS_VS_NUM 8
#define MTS_VS_PATH "/mnt/hpt"
#define MTS_VS_DISK_NUM 8
#define MTS_VS_ENTRY_SIZE sizeof(vs_entry_t)
#define MTS_VS_ENTRIES_PER_CHUNK (MTS_VS_CHUNK_SIZE / MTS_VS_ENTRY_SIZE) 
#define MTS_VS_G_SIZE (MTS_VS_NUM * 512UL * 1024UL * 1024UL * 1024UL)
#define MTS_VS_SIZE (MTS_VS_G_SIZE / MTS_VS_NUM)
#define MTS_VS_CHUNK_SIZE (512UL * 1024UL)
/* w/ io_uring, 512KB is the best size 128KB * QD(4) */
#define MTS_VS_CHUNK_NUM (MTS_VS_SIZE / MTS_VS_CHUNK_SIZE)
#define MTS_VS_HIGH_MARK (MTS_VS_CHUNK_NUM * 75 / 100)
#define MTS_VS_LOW_MARK (MTS_VS_CHUNK_NUM * 65 / 100)
/* Number of CHUNKS, not bytes */

#define READ_IO_SIZE 4096UL

/* io_uring */
#define W_QD 4
#define R_QD 64
#define GC_QD 8
#define IO_URING_WRITE	    1
#define IO_URING_READ	    1
#define IO_URING_SCAN	    1
#define IO_URING_GC	    1
#define IO_URING_WRING_NUM MTS_THREAD_NUM
#define IO_URING_RRING_NUM MTS_THREAD_NUM
#define IO_URING_SRING_NUM MTS_THREAD_NUM
#define IO_COMPLETER_NUM 8

/* DRAM Cache */
#define MTS_DRAMCACHE 1
#define MTS_DRAMCACHE_NUM 1
#define MTS_DRAMCACHE_SIZE ((20UL * 1024UL * 1024UL * 1024UL))
#define MTS_DRAMCACHE_RATIO 16UL
#define MTS_ACTIVE_LIST_SIZE (MTS_DRAMCACHE_SIZE / MTS_DRAMCACHE_RATIO)
#define MTS_INACTIVE_LIST_SIZE (MTS_DRAMCACHE_SIZE - MTS_ACTIVE_LIST_SIZE)
#define MTS_RECLAIM_PAGES_NUM 512
#define MTS_CACHEQUEUE_NUM MTS_THREAD_NUM
/* Set to the same value as the number of MTS thread*/

/* Value location */
enum {
    PRE_VALUESTORAGE_VAL = -2,
    NEWLY_UPDATED_VAL,
    CLEAN_ENTRY = 0,
    DCACHE_VAL,
    OPLOG_VAL,
    VALUESTORAGE_VAL,
};

/* LinkedList */
enum { NONE,
    ACTIVE_LIST,
    INACTIVE_LIST,
};

/* CacheThread */
enum { CT_LOOKUP,
    CT_SCAN,
};

enum { NORMAL_WRITE,
    FORCED_WRITE
};

enum { OL_INSERT,
    OL_UPDATE,
};

enum {KEYINDEX,
    OPLOG,
    ADDRESSTABLE,
    VALUESTORAGE,
    LINK,
    DCACHE,
    TOTAL_GET,
};

//#define MTS_VS_HIGH_MARK (MTS_VS_CHUNK_NUM - ((MTS_OPLOG_HIGH_MARK - MTS_OPLOG_LOW_MARK) / MTS_VS_CHUNK_SIZE))
//#define MTS_VS_LOW_MARK (MTS_VS_CHUNK_NUM * 10 / 100)

#endif /* _MTS_CONFIG_H */
