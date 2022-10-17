#ifndef MTS_OPLOG_H
#define MTS_OPLOG_H

#include <libpmemobj.h>
#include <thread>
#include <mutex>
#include <cassert>
#include <numeric>
#include <random>
#include "../lib/TSOpLog/nvlog.h"
#include "../lib/TSOpLog/oplog.h"
#include "../lib/TSOpLog/ckptlog.h"
#include "../lib/TSOpLog/util.h"
#include "../lib/TSOpLog/nvm.h"
#include "MTSImpl.h"
#include "SpinLock.h"

#define NEED_RECLAIM 0x00
#define MTS_MAX_OPERAND_SIZE (256 - sizeof(op_entry_hdr_t))

class AddressTable;
class ValueStorage;
class MTSThread;

typedef struct at_entry at_entry_t;

typedef struct mts_op_entry {
    Val_t val;
    unsigned char __reserved[KV_SIZE - sizeof(val) - (L1_CACHE_BYTES * 1)];
    at_entry_t *opa;	/* for table address */
    Key_t key;
    size_t size;
    unsigned char __reserved2[(L1_CACHE_BYTES * 1) - sizeof(opa) - sizeof(size) - sizeof(key)];
} __nvm ____ptr_aligned op_entry_t;

typedef struct mts_op_info {
    unsigned int entry_size;
    Key_t key;
    Val_t val;
} op_info_t;   

class OpLog {
    private:
	SpinLock spinlock;
	std::atomic<bool> reclaim_lock;

	int g_oplog_id;
	ts_nvm_root_obj_t *nvm_root_obj;
	ts_oplog_t *working_oplog;
	ts_oplog_t *reclaimed_oplog;

	int need_recovery;
	op_info_t op_info;
	
    public:
	ts_oplog_t oplog1;
	ts_oplog_t oplog2;
	ts_oplog_t dummy;

	mutable std::mutex mutex_;
	OpLog(const char *path, int ol_id);
	~OpLog();

	std::atomic<uint64_t> total_ol_write_count;
	
	/* Enqueue Steps */
	op_entry_t *enq(Key_t key, Val_t val, int type);
	op_entry_t *put_ol_entry(ts_oplog_t &oplog, Key_t key, Val_t val);
	ts_oplog_t *get_another_oplog(ts_oplog_t *oplog);
	op_entry_t *oplog_enq(ts_oplog_t *oplog, op_info_t *op_info);
	op_entry_t *nvlog_enq(ts_nvlog_t *nvlog, unsigned int obj_size);
	void oplog_enq_persist(ts_oplog_t *oplog);
	
	/* MTS Consistency */
	void link_to_at(op_entry_t *op_entry, at_entry_t *at_entry);
	void unlink_to_at(at_entry_t *at_entry);

	/* Recalim and Dequeue */
	std::thread *reclaim_thread;
	void reclaim(volatile int oplog_id);
	op_entry_t *oplog_peek_head(ts_oplog_t *oplog);
	op_entry_t *oplog_deq(ts_oplog_t *oplog);
	void oplog_deq_persist(ts_oplog_t *oplog);

	/* util */
	op_entry_t *oplog_at(ts_nvlog_t *nvlog, unsigned long cnt);
	unsigned long nvlog_index(ts_nvlog_t *nvlog, unsigned long cnt);
	ValueStorage *pick_valuestorage(int oplog_id);
};

extern ts_nvm_root_obj_t *__g_root_obj;

#endif /* MTS_OPLOG_H */
