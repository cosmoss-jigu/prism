#ifndef MTS_MTS_H
#define MTS_MTS_H

#include <utility>
#include <vector>
#include <algorithm>
#include <thread>
#include <set>
#include <boost/lockfree/spsc_queue.hpp>
#include <queue>
#include <random>
#include <cmath>

#include "primitives.h"
#include "util.h"
#include "ordo_clock.h"
#include "common.h"
#include "mts-config.h"
#include "KeyIndex.h"
#include "AddressTable.h"
#include "OpLog.h"
#include "ValueStorage.h"
#include "CacheThread.h"
#include "MTSThread.h"
#include "AIO.h"
#include "SpinLock.h"

#if PACTREE
#include "pactree.h"
#endif

class AddressTable;
class OpLog;
class ValueStorage;
class CacheThread;

typedef struct cq_entry cq_entry_t;
typedef struct vs_entry vs_entry_t;
typedef struct aio_struct aio_struct_t;
typedef struct aio_thread_state aio_thread_state_t;

extern bool cqReady[MTS_CACHEQUEUE_NUM];
extern bool fqReady[MTS_CACHEQUEUE_NUM];

extern std::set<MTSThread *> g_MTSThreadSet;
extern std::vector<KeyIndex *> g_perNumaKeyIndex;
extern std::vector<OpLog *> g_perNumaOpLog;
extern std::vector<AddressTable*> g_perNumaAddressTable;
extern std::vector<ValueStorage *> g_perNumaValueStorage;

extern std::queue<std::vector<cq_entry_t *> *> g_cacheQueue[MTS_CACHEQUEUE_NUM];
extern std::queue<at_entry_t *> g_cacheFreeQueue[MTS_CACHEQUEUE_NUM];

extern uint64_t scan_latency[IO_URING_RRING_NUM];

class MTSImpl {
    private:
	static thread_local int threadNumaNode;
	static int totalNumaActive;
	std::atomic<uint32_t> numThreads;
	std::atomic<uint32_t> cacheAccess;
	std::atomic<uint32_t> cacheHit;
	std::atomic<uint32_t> cacheMiss;

	uint64_t ready_timestamp[MTS_VS_NUM][IO_URING_RRING_NUM][R_QD];
	uint64_t work_timestamp[MTS_VS_NUM][IO_URING_RRING_NUM][R_QD];

	std::vector<std::vector<OpForm *>> input_q;

	std::thread *DramCacheThread;
	std::thread *IOCompleterThread[IO_COMPLETER_NUM];
	void IOCompleterThreadExec(int init_id);

	aio_struct_t *object_combiner[MTS_VS_NUM][IO_URING_RRING_NUM];
	aio_thread_state_t *th_state[MTS_THREAD_NUM];
    
    public:
	explicit MTSImpl(int numNuma);
	~MTSImpl();

	bool insert(Key_t &key, Val_t val);
	bool update(Key_t &key, Val_t val);
	bool remove(Key_t &key);
	Val_t lookup(Key_t &key);
	uint64_t scan(Key_t &startKey, int range, std::vector<Val_t> &result);
	bool recover(Key_t &startKey);

	int get_val_pos(at_entry_t *at_entry, int *cur_vs_id);
	bool is_cached(at_entry_t *at_entry);
	void cache_kv_items(Val_t val, at_entry_t *at_entry, int curMTSThread);
	void cache_kv_items(std::vector<cq_entry_t *> *cq_entry_vec, int curMTSThread);
	void cache_kv_items(std::vector<cq_entry_t *> *cq_entry_vec, vs_entry_t *vs_entry, int ops);
	void cache_free_kv_items(at_entry_t *at_entry, int curMTSThread);
	uint64_t complete_pending_ios(ValueStorage *vs, int ring_idx, int ops, std::vector<cq_entry_t *> *cq_entry_vec);
	void complete_pending_ios(ValueStorage *vs, int ring_idx);

	static KeyIndex *createKeyIndex();
	static OpLog *createOpLog(const char *path, int ol_id);
	static AddressTable *createAddressTable();
	static AddressTable *createAddressTable(const char *path, int at_id);
	static ValueStorage *createValueStorage(const char *path, int vs_id);


	static int getThreadNuma();

	void createCacheThread();
	void createIOCompleterThread();

	void registerThread();
	void unregisterThread();
	
	std::atomic<uint64_t> total_get_cnt;
	std::atomic<uint64_t> total_dcache_hit_cnt;
	std::atomic<uint64_t> total_oplog_hit_cnt; 
	std::atomic<uint64_t> total_valuestorage_hit_cnt;

	std::atomic<uint64_t> total_ki_time;
	std::atomic<uint64_t> total_at_time;
	std::atomic<uint64_t> total_ol_time;
	std::atomic<uint64_t> total_vs_time;
	std::atomic<uint64_t> total_link_time;
};

#endif //MTS_MTS_H
