#include <iostream>
#include <zconf.h>
#include <cassert>
#include <mutex>
#include <ordo_clock.h>
#include <time.h>
#include "numa.h"
#include "MTSImpl.h"
#include "numa-config.h"
#include <sys/time.h>

#ifdef MTS_STATS_GET
#define MTS_RESET_GET_COUNTERS()    \
{				    \
    total_get_cnt = 0;		    \
    total_dcache_hit_cnt = 0;	    \
    total_oplog_hit_cnt = 0;	    \
    total_valuestorage_hit_cnt = 0; \
}
#define INC_GET_CNT() (curMTSThread->get_cnt++)
#define INC_DCACHE_HIT_CNT() (curMTSThread->dcache_hit_cnt++)
#define INC_OPLOG_HIT_CNT() (curMTSThread->oplog_hit_cnt++)
#define INC_VALUESTORAGE_HIT_CNT() (curMTSThread->valuestorage_hit_cnt++)
#define INC_VALUESTORAGE_HIT_CNT2(x) (curMTSThread->valuestorage_hit_cnt+=x)
#else
#define MTS_RESET_GET_COUNTERS()
#define INC_GET_CNT() 
#define INC_DCACHE_HIT_CNT() 
#define INC_OPLOG_HIT_CNT() 
#define INC_VALUESTORAGE_HIT_CNT() 
#define INC_VALUESTORAGE_HIT_CNT2(x)
#endif

#ifdef MTS_STATS_LATENCY
#define MTS_SET_TIMER(timestamp)    \
{				    \
    timestamp = read_tscp();	    \
}
#else
#define MTS_SET_TIMER(x)
#endif

#define MAX(a, b) (((a) > (b)) ? (a) : (b))

std::random_device rd;
std::mt19937 gen(rd());

std::queue<std::vector<cq_entry_t *> *> g_cacheQueue[MTS_CACHEQUEUE_NUM];
std::queue<at_entry_t *> g_cacheFreeQueue[MTS_CACHEQUEUE_NUM];
int g_phase[MTS_THREAD_NUM]; // 3: loading, 2: warm_up, 1: exec workload;
std::atomic<uint64_t> batched_cnt = 0;
std::atomic<uint64_t> batched_io = 0;

uint64_t scan_latency[IO_URING_RRING_NUM];
bool cqReady[MTS_CACHEQUEUE_NUM];
bool fqReady[MTS_CACHEQUEUE_NUM];
volatile bool ctInitialized = false;
volatile bool iocInitialized = false;
volatile bool ioc_lookup = false;
volatile bool ioc_scan = false;

std::atomic<bool> g_endMTS;
thread_local int MTSImpl::threadNumaNode = -1;
int MTSImpl::totalNumaActive = 0;
uintptr_t addresstable_interval;

SpinLock g_mutex_;

std::set<MTSThread *> g_MTSThreadSet;
std::vector<KeyIndex *> g_perNumaKeyIndex(MTS_KEYINDEX_NUM);;
std::vector<AddressTable *> g_perNumaAddressTable(MTS_AT_NUM);
std::vector<OpLog *> g_perNumaOpLog(MTS_OPLOG_NUM);
std::vector<ValueStorage *> g_perNumaValueStorage(MTS_VS_NUM);
thread_local MTSThread* curMTSThread = NULL;

int MTSImpl::getThreadNuma() {
    int chip; 
    int core;
    if(threadNumaNode == -1) {
	tacc_rdtscp(&chip, &core);
	threadNumaNode = chip;
	if(threadNumaNode > 8)
	    assert(0);
    }   
    if(totalNumaActive <= threadNumaNode)
	return 0;
    else
	return threadNumaNode;
}

void MTSImpl::cache_kv_items(Val_t val, at_entry_t *at_entry, int curThreadId) {
    std::atomic<int> qid = curThreadId;
    while(true) {
	if(smp_cas(&cqReady[qid], true, false)) {
	    std::vector<cq_entry_t *>* cq_entry_vec = new std::vector<cq_entry_t *>;
	    cq_entry_t *cq_entry = new cq_entry_t;
	    cq_entry->at_entry = at_entry;
	    cq_entry->val = val;
	    cq_entry->ops = CT_LOOKUP;
	    cq_entry_vec->push_back(cq_entry);
	    g_cacheQueue[qid].push(cq_entry_vec);
	    smp_cas(&cqReady[qid], false, true);
	    break;
	}
    }
}

void MTSImpl::cache_kv_items(std::vector<cq_entry_t *> *cq_entry_vec, vs_entry_t *vs_entry, int ops) {
    cq_entry_t *cq_entry = new cq_entry_t;
    cq_entry->at_entry = vs_entry->at_entry;
    cq_entry->key = vs_entry->key;
    cq_entry->val = vs_entry->val;
    cq_entry->ops = ops;
    cq_entry_vec->push_back(cq_entry);
    ts_trace(TS_INFO, "at_entry %p val %lu ops %d size %d\n",
	    cq_entry->at_entry, cq_entry->val, cq_entry->ops, cq_entry_vec->size());
}

void MTSImpl::cache_kv_items(std::vector<cq_entry_t *> *cq_entry_vec, int curThreadId) {
    std::atomic<int> qid = curThreadId;
    while(true) {
	if(smp_cas(&cqReady[qid], true, false)) {
	    g_cacheQueue[qid].push(cq_entry_vec);
	    smp_cas(&cqReady[qid], false, true);
	    break;
	}
    }
}

void MTSImpl::cache_free_kv_items(at_entry_t *at_entry, int curThreadId) {
    std::atomic<int> qid = curThreadId;
    if(is_cached(at_entry)) {
	while(true) {
	    if(smp_cas(&fqReady[qid], true, false)) {
		g_cacheFreeQueue[qid].push(at_entry);
		smp_cas(&fqReady[qid], false, true);
		break;
	    }
	}
    }
}

void DramCacheThreadExec() {
    while(ctInitialized == false){}

    CacheThread ct;
    int i = 0;
    int j = 0;

    while(!g_endMTS) {
	/* free cached entries */
	if(!g_cacheFreeQueue[i].empty()) {
	    while(true) {
		if(smp_cas(&fqReady[i], true, false)) {
		    while(!g_cacheFreeQueue[i].empty()) {
			auto at_entry = g_cacheFreeQueue[i].front();
			g_cacheFreeQueue[i].pop();
			ct.freeOperation(at_entry);
		    }
		    smp_cas(&fqReady[i], false, true);
		    break;
		}
	    }
	}
	i++;
	if(i == MTS_CACHEQUEUE_NUM)
	    i = 0;

	/* cache entries */
	if(!g_cacheQueue[j].empty()) {
	    while(true) { 
		if(smp_cas(&cqReady[j], true, false)) { 
		    ts_trace(TS_INFO, "CacheThread 0 | cqReady %p %d\n", &cqReady[j], cqReady[j]);
		    while(!g_cacheQueue[j].empty()) {
			auto cq_entry_vec = g_cacheQueue[j].front();
			ct.cacheOperation(cq_entry_vec);
			g_cacheQueue[j].pop();
			free(cq_entry_vec);
			ts_trace(TS_INFO, "CacheThread 2 | cnt %u %u\n", g_cacheQueue[j].size(), g_cacheQueue[j].empty());
		    }
		    ts_trace(TS_INFO, "CacheThread 3 | cqReady %p %d\n", &cqReady[j], cqReady[j]);
		    smp_cas(&cqReady[j], false, true);
		    break;
		}
	    }
	}
	j++;
	if(j == MTS_CACHEQUEUE_NUM)
	    j = 0;
    }
}

/* for LOOKUP() */
void MTSImpl::complete_pending_ios(ValueStorage *vs, int ring_idx) {
    struct io_uring_cqe *r_cqe;
    int entry_idx, ret;
    vs_entry_t *vs_entry;

    std::vector<cq_entry_t *> *cq_entry_vec = new std::vector<cq_entry_t *>;

    for(entry_idx = 0; entry_idx < vs->pending_ios[ring_idx]; entry_idx++) {

	ret = io_uring_wait_cqe(&vs->r_ring[ring_idx], &r_cqe);
	if(ret < 0) {
	    ts_trace(TS_ERROR, "[GET_VAL_ASYNC] io_uring_wait_cqe failed! ret=%d %s\n", ret, strerror(-ret));
	    exit(EXIT_FAILURE);
	}
	if(r_cqe->res < 0) { 
	    ts_trace(TS_ERROR, "[GET_VAL_ASYNC] Error in async operation ret=%d, fd %d wanted READ_IO_SIZE: %s\n",
		    r_cqe->res, vs->fd[0], strerror(-r_cqe->res));
	    exit(EXIT_FAILURE);
	}   

	vs_entry = (vs_entry_t *)io_uring_cqe_get_data(r_cqe);
	ts_trace(TS_INFO, "V lookup key %lu val %lu %p\n", vs_entry->key, vs_entry->val, vs_entry->at_entry);

#ifdef MTS_STATS_LATENCY
	uint64_t start, end, elapsed_time;
	at_entry_t *temp = vs_entry->at_entry;
	end = read_tscp();
	start = temp->timestamp; 
	if(start) {
	    elapsed_time = end - start;
	    add_timing_stat(elapsed_time, VALUESTORAGE_VAL);
	    temp->timestamp = 0;
	}
#endif

	cache_kv_items(cq_entry_vec, vs_entry, CT_LOOKUP);
	io_uring_cqe_seen(&vs->r_ring[ring_idx], r_cqe);
    }

    vs->pending_ios[ring_idx] = 0;

    if(!cq_entry_vec->empty()) {
	int qid = ring_idx;
	while(true) {
	    if(smp_cas(&cqReady[qid], true, false)) {
		g_cacheQueue[qid].push(cq_entry_vec);
		smp_cas(&cqReady[qid], false, true);
		break;
	    }    
	}
    } else {
	free(cq_entry_vec);
    }

#ifdef MTS_STATS_GET
    int batched = entry_idx;
    total_valuestorage_hit_cnt.fetch_add(batched);
    if(batched != 0) {
	batched_io += batched;
	batched_cnt++;	
    }
#endif
}

/* for SCAN() */
uint64_t MTSImpl::complete_pending_ios(ValueStorage *vs, int ring_idx, int ops, std::vector<cq_entry_t *> *cq_entry_vec) {
    struct io_uring_cqe *r_cqe;
    int entry_idx, ret;
    vs_entry_t *vs_entry;

    for(entry_idx = 0; entry_idx < vs->pending_ios[ring_idx]; entry_idx++) {
	ret = io_uring_wait_cqe(&vs->r_ring[ring_idx], &r_cqe);
	if(ret < 0) {
	    ts_trace(TS_ERROR, "[GET_VAL_ASYNC] io_uring_wait_cqe failed! ret=%d %s\n", ret, strerror(-ret));
	    exit(EXIT_FAILURE);
	}
	if(r_cqe->res < 0) { 
	    ts_trace(TS_ERROR, "[GET_VAL_ASYNC] Error in async operation ret=%d, fd %d wanted READ_IO_SIZE: %s\n",
		    r_cqe->res, vs->fd[0], strerror(-r_cqe->res));
	    exit(EXIT_FAILURE);
	}   

	vs_entry = (vs_entry_t *)io_uring_cqe_get_data(r_cqe);
	ts_trace(TS_INFO, "V lookup key %lu val %lu %p\n", vs_entry->key, vs_entry->val, vs_entry->at_entry);

	cache_kv_items(cq_entry_vec, vs_entry, ops);
	io_uring_cqe_seen(&vs->r_ring[ring_idx], r_cqe);
    }
#ifdef MTS_STATS_GET
    int batched = entry_idx;
    total_valuestorage_hit_cnt.fetch_add(batched);
    if(batched != 0) {
	batched_io += batched;
	batched_cnt++;	
    }
#endif

#ifdef MTS_STATS_LATENCY
    uint64_t end, elapsed_time;
    end = read_tscp();
    uint64_t start = scan_latency[ring_idx];
    elapsed_time = end - start;
    ts_trace(TS_INFO, "%lu %lu %lu\n", cycles_to_us(elapsed_time), end, start);
    add_timing_stat(elapsed_time, VALUESTORAGE_VAL);
#endif

    vs->pending_ios[ring_idx] = 0;

    return 0; 
}

void MTSImpl::IOCompleterThreadExec(int init_id) {
    int ops;
    int ret = 0;
    int entry_idx;
    uint64_t start = 0;

    while(iocInitialized == false) {}

    ts_trace(TS_INFO, "IOCompleterThread begins with %d\n", init_id);

    ops = CT_LOOKUP;

    int vs_id;
    int ring_idx = 0;

    while(!ioc_scan) {
	for(vs_id = init_id; vs_id < MTS_VS_NUM; vs_id += IO_COMPLETER_NUM) {
	    ValueStorage *vs = g_perNumaValueStorage[vs_id];
	    smp_mb();
	    if(vs->pending_ios[ring_idx]) {
		complete_pending_ios(vs, ring_idx);
	    }
	}
    }

    if(ioc_scan) {
	ops = CT_SCAN;

	while(!g_endMTS) {
	    for(int ring_idx = init_id; ring_idx < IO_URING_RRING_NUM; ring_idx += IO_COMPLETER_NUM) {
		std::vector<cq_entry_t *> *cq_entry_vec = new std::vector<cq_entry_t *>;
		cq_entry_vec->reserve(R_QD);
		for(vs_id = 0; vs_id < MTS_VS_NUM; vs_id++) {
		    ValueStorage *vs = g_perNumaValueStorage[vs_id];
		    smp_mb();
		    if(vs->pending_ios[ring_idx]) {
			complete_pending_ios(vs, ring_idx, ops, cq_entry_vec);
		    }
		}

		if(!cq_entry_vec->empty()) {
		    int qid = ring_idx;
		    while(true) {
			if(smp_cas(&cqReady[qid], true, false)) {
			    g_cacheQueue[qid].push(cq_entry_vec);
			    smp_cas(&cqReady[qid], false, true);
			    break;
			}    
		    }
		} else {
		    free(cq_entry_vec);
		}
	    }
	}
    }

    else {
	ts_trace(TS_ERROR, "Wrong Operations...\n");
	exit(EXIT_FAILURE);
    }
}

void MTSImpl::createCacheThread() {
    g_mutex_.lock();
    ctInitialized = false;
    for (int i = 0; i < MTS_CACHEQUEUE_NUM; i++) {
	cqReady[i] = true;
	fqReady[i] = true;
    }
    ctInitialized = true;
    DramCacheThread = new std::thread(DramCacheThreadExec);

    g_mutex_.unlock();
}

void MTSImpl::createIOCompleterThread() {
    g_mutex_.lock();
    iocInitialized = false;
    for (int i = 0; i < IO_COMPLETER_NUM; i++) {
	IOCompleterThread[i] = new std::thread(&MTSImpl::IOCompleterThreadExec, this, i);
    }
    g_mutex_.unlock();
}

MTSImpl::MTSImpl(int numNuma) {
    char path[100];
    ts_trace(TS_ERROR, "### PRISM INFO. ============================================================\n");
    ts_trace(TS_ERROR, "NUM_SOCKET: %d, NUM_THREADS: %d\n", NUM_SOCKET, MTS_THREAD_NUM);
    ts_trace(TS_ERROR, "KV-item Size: %lu B, READ_IO_SIZE: %lu KB, WRITE_CHUNK_SIZE: %lu KB\n",
	    KV_SIZE, READ_IO_SIZE/1024, MTS_VS_CHUNK_SIZE/1024);
    ts_trace(TS_ERROR, "SVC Size: %lu GB\n", MTS_DRAMCACHE_SIZE/1024/1024/1024);
    ts_trace(TS_ERROR, "PWB Size: %lu GB (# = %u)\n", MTS_OPLOG_G_SIZE/1024/1024/1024, MTS_OPLOG_NUM);
    ts_trace(TS_ERROR, "VS: %u, Disks: %d\n", MTS_VS_NUM, MTS_VS_DISK_NUM);
    ts_trace(TS_ERROR, "READ_QD: %d, WRITE_QD: %d\n", R_QD, W_QD);
    ts_trace(TS_ERROR, "IO_COMPLETION_THREAD: %d\n", IO_COMPLETER_NUM);
    ts_trace(TS_ERROR, "### RESULTS ================================================================\n");

    assert(numNuma <= NUM_SOCKET);
    assert(MTS_VS_HIGH_MARK > 0);
    totalNumaActive = numNuma;
    g_endMTS = false;

    numThreads = 0;

    for (int i = 0; i < MTS_KEYINDEX_NUM; i++) {
	g_perNumaKeyIndex[i] = MTSImpl::createKeyIndex();
	sleep(1);
	ts_trace(TS_INFO, "[PRISMImpl] Create KeyIndex %d\n", i);
    }

    for (int i = 0; i < MTS_OPLOG_NUM; i++) {
	if(i < (MTS_OPLOG_NUM / 2))
	    sprintf(path, NVHEAP_POOL_PATH"0/prism/pwb%d", i);
	else sprintf(path, NVHEAP_POOL_PATH"1/prism/pwb%d", i);
	g_perNumaOpLog[i] = MTSImpl::createOpLog(path, i);
	ts_trace(TS_INFO, "[PRISMImpl] Create PWB %d\n", i);
    }

    for (int i = 0; i < MTS_AT_NUM; i++) {
	if(i < (MTS_AT_NUM / 2))
	    sprintf(path, MTS_AT_PATH"0/prism/hsit%d", i);
	else sprintf(path, MTS_AT_PATH"1/prism/hsit%d", i);
	g_perNumaAddressTable[i] = MTSImpl::createAddressTable(path, i);
	ts_trace(TS_INFO, "[PRISMImpl] Create HIST %d\n", i);
    }

    for(int i = 0; i < MTS_VS_NUM; i++) {
	int partition = i % MTS_VS_DISK_NUM;
	sprintf(path, MTS_VS_PATH"%d/prism/valuestorage%d", partition, i);
	g_perNumaValueStorage[i] = MTSImpl::createValueStorage(path, i);
	ts_trace(TS_INFO, "[PRISMImpl] Create ValueStorage %d\n", g_perNumaValueStorage[i]->get_vs_id());

	for(int ring_idx = 0; ring_idx < IO_URING_RRING_NUM; ring_idx++) {
	    object_combiner[i][ring_idx] = (aio_struct_t *)get_aligned_memory(L1_CACHE_BYTES, sizeof(aio_struct_t));
	    aio_struct_init(object_combiner[i][ring_idx]);
	    object_combiner[i][ring_idx]->is_working = false;
	}
    }

    for(int i = 0; i < MTS_THREAD_NUM; i++) {
	th_state[i] = (aio_thread_state_t *)get_aligned_memory(L1_CACHE_BYTES, sizeof(aio_thread_state_t));
	aio_thread_state_init(th_state[i]);
    }

    ts_trace(TS_INFO, "[PRISMImpl] Create Cache-queue%d\n", MTS_DRAMCACHE_NUM);
    createCacheThread();
    createIOCompleterThread();

    for(int i = 0; i < MTS_THREAD_NUM; i++)
	g_phase[i] = 3;

    MTS_RESET_GET_COUNTERS();
}

MTSImpl::~MTSImpl() {
    ctInitialized = true;
    iocInitialized = true;

    g_endMTS = true;
    ioc_scan = true;
    ts_trace(TS_INFO, "[~PRISMImpl] g_endMTS\n");

    //terminate iocompletionthread
    g_mutex_.lock();
    for(int i = 0; i < IO_COMPLETER_NUM; i++) {
	if(IOCompleterThread[i]->joinable()) {
	    IOCompleterThread[i]->join();
	    delete IOCompleterThread[i];
	}
    }
    g_mutex_.unlock();

    for(auto mt : g_MTSThreadSet) {
	while(true) {
	    if(mt->getFinish()) {
		g_mutex_.lock();
		g_MTSThreadSet.erase(mt);
		g_mutex_.unlock();
		break;
	    }
	}
    }

    // terminate cacheThread 
    g_mutex_.lock();
    for(int i = 0; i < MTS_DRAMCACHE_NUM; i++) {
	if(DramCacheThread->joinable()) {
	    DramCacheThread->join();
	    delete DramCacheThread;
	}
    }
    g_mutex_.unlock();
    for(int i = 0; i < MTS_KEYINDEX_NUM; i++) {
	delete g_perNumaKeyIndex[i];
    }

    for(int i = 0; i < MTS_AT_NUM; i++) {
	delete g_perNumaAddressTable[i];
    }

    for(int i = 0; i < MTS_OPLOG_NUM; i++) {
	delete g_perNumaOpLog[i];
    }

    uint64_t vs_total_write_count = 0;
    uint64_t ol_total_write_count = 0;

    for(int i = 0; i < MTS_VS_NUM; i++) {
	ts_trace(TS_INFO, "[~PRISMImpl] VS_ID: %d check_all_chunks()\n", g_perNumaValueStorage[i]->get_vs_id());
	vs_total_write_count += g_perNumaValueStorage[i]->total_vs_write_count;

	delete g_perNumaValueStorage[i];
    }

    for(int i = 0; i < MTS_OPLOG_NUM; i++) {
	ol_total_write_count += g_perNumaOpLog[i]->total_ol_write_count;
    }

#ifdef MTS_STATS_WAF
    uint64_t vs_write = vs_total_write_count * 512 / 1024;
    uint64_t ol_write = ol_total_write_count * KV_SIZE / 1024 / 1024;
    double waf = (double)vs_write / (double)ol_write;

    std::cout << "### SSD WAF ================================================================" << std::endl;
    std::cout << "TOTAL_VALUESTORAGE_WRITE(MB)\t" << vs_write << std::endl;
    std::cout << "TOTAL_STORED_DATA(MB)\t" << ol_write << std::endl;
    std::cout << std::fixed;
    std::cout.precision(2);
    std::cout << "SSD-level WAF\t" << waf << std::endl;

#endif

#ifdef MTS_STATS_GET
    std::cout << std::fixed;
    std::cout.precision(2);

    const double avg_batched = static_cast<double>(batched_io) / static_cast<double>(batched_cnt);
    std::cout << "### I/O Batching ===========================================================" << std::endl;
    std::cout << "Avg_I/O_Batching\t" << avg_batched << std::endl;

    const double dcache_hit_ratio = static_cast<double>(total_dcache_hit_cnt) / static_cast<double>(total_get_cnt) * 100;
    const double oplog_hit_ratio = static_cast<double>(total_oplog_hit_cnt) / static_cast<double>(total_get_cnt) * 100;
    const double valuestorage_hit_ratio = static_cast<double>(total_valuestorage_hit_cnt) / static_cast<double>(total_get_cnt) * 100;
    std::cout << "### GET STATS ==============================================================" << std::endl;
    std::cout << "SVC_Cnt\t" << total_dcache_hit_cnt << "\t\t" << dcache_hit_ratio << std::endl;
    std::cout << "PWB_Cnt\t" << total_oplog_hit_cnt << "\t\t" << oplog_hit_ratio << std::endl;
    std::cout << "VS_Cnt\t" << total_valuestorage_hit_cnt << "\t\t" << valuestorage_hit_ratio << std::endl;
    std::cout << "GET_Cnt\t" << total_dcache_hit_cnt + total_oplog_hit_cnt + total_valuestorage_hit_cnt << std::endl;
#endif

#ifdef MTS_STATS_LATENCY
    std::cout << "### LATENCY (us) ===========================================================" << std::endl;
    print_stats();
    //std::cout << "============================================================================" << std::endl;
#endif
}

int MTSImpl::get_val_pos(at_entry_t *at_entry, int *cur_vs_id) {
    int tag = get_tag((intptr_t)at_entry->val_addr);

    if((at_entry->vs_idx.vs_id > -1) && (at_entry->vs_idx.vs_offset > -1)) {
	if(tag == DCACHE_VAL)
	    return DCACHE_VAL;
	else {
	    vs_idx_t vs_idx = at_entry->vs_idx;
	    *cur_vs_id = vs_idx.vs_id;
	    return VALUESTORAGE_VAL;
	}
    }

    if(tag == OPLOG_VAL)
	return OPLOG_VAL;
}

bool MTSImpl::is_cached(at_entry_t *at_entry) {
    int tag = get_tag((intptr_t)at_entry->val_addr);
    if(tag == DCACHE_VAL)
	return true;
    else return false;
}

bool MTSImpl::insert(Key_t &key, Val_t val) {
    bool ret;
    int curThreadId = curMTSThread->getThreadId();

    KeyIndex &keyindex = *g_perNumaKeyIndex[0];
    AddressTable &addresstable = *g_perNumaAddressTable[curThreadId];
    OpLog &oplog = *g_perNumaOpLog[curThreadId];

    op_entry_t *op_entry = nullptr;
    at_entry_t *at_entry = nullptr;

    /* 1. Add a new op_entry(oplog->enq()) */
    op_entry = oplog.enq(key, val, OL_INSERT);
    ts_trace(TS_INFO, "[INSERT-1] key: %lu, at_entry: %p, op_entry: %p\n", key, at_entry, op_entry);

    /* 2. Add a new at_entry */
    at_entry = addresstable.assign(key);
    ts_trace(TS_INFO, "[INSERT-2] key: %lu, at_entry: %p, op_entry: %p\n", key, at_entry, op_entry);

    /* 3. Link the at_entry with op_entry */
    oplog.link_to_at(op_entry, at_entry);
    addresstable.link_to_ol(at_entry, op_entry);
    ts_trace(TS_INFO, "[INSERT-3] key: %lu, at_entry: %p, op_entry: %p\n", key, at_entry, op_entry);

    /* 4. Add a new index_entry */
    ret = keyindex.insert(key, (void *)at_entry); 
    ts_trace(TS_INFO, "key %lu at_entry %p\n", key, at_entry);

    return ret;
}

bool MTSImpl::update(Key_t &key, Val_t val) {
    int past_vs_id = 0;
    int past_vs_offset = 0;
    int curThreadId = curMTSThread->getThreadId();

    KeyIndex &keyindex = *g_perNumaKeyIndex[0];
    AddressTable &addresstable = *g_perNumaAddressTable[curThreadId];
    OpLog &oplog = *g_perNumaOpLog[curThreadId];

    op_entry_t *op_entry = nullptr;
    at_entry_t *at_entry = nullptr;

#ifdef MTS_STATS_LATENCY
    uint64_t start, end;
    MTS_SET_TIMER(start);
#endif

    at_entry = (at_entry_t *)keyindex.lookup(key);
    if((uintptr_t)at_entry == 0x0) {
	ts_trace(TS_ERROR, "[UPDATE] keyindex.lookup returns non-exist key:%lu \n", key);
	return 0;
    }
    ts_trace(TS_INFO, "[UPDATE-1] at_entry: %p key: %lu\n", at_entry, key);
    
    op_entry = oplog.enq(key, val, OL_UPDATE);
    ts_trace(TS_INFO, "[UPDATE-2] at_entry: %p, op_entry addr: %p\n", at_entry, op_entry);

    oplog.link_to_at(op_entry, at_entry);
    addresstable.link_to_ol(at_entry, op_entry, &past_vs_id, &past_vs_offset); 
    ts_trace(TS_INFO, "[UPDATE-3] at_entry: %p, op_entry: %p\n", at_entry, op_entry);

#ifdef MTS_STATS_WAF
    oplog.total_ol_write_count++;
#endif

#ifdef MTS_STATS_LATENCY
    MTS_SET_TIMER(end);
    add_timing_stat((end-start), OPLOG_VAL);
#endif

    if (!(past_vs_offset < 0) && !(past_vs_id < 0)) {
	int chunk_offset = past_vs_offset / MTS_VS_ENTRIES_PER_CHUNK;
	int entry_offset = past_vs_offset % MTS_VS_ENTRIES_PER_CHUNK;
	ts_trace(TS_INFO, "[UPDATE-4] at_entry: %p, PAST_VS_ID: %d, CHUNK_OFFSET: %d, ENTRY_OFFSET: %d\n", 
		at_entry, past_vs_id, chunk_offset, entry_offset);

	ValueStorage *valuestorage = g_perNumaValueStorage[past_vs_id];
	valuestorage->unlink_to_at(chunk_offset, entry_offset, at_entry);
    }

    cache_free_kv_items(at_entry, curThreadId);

    return true;
}

Val_t MTSImpl::lookup(Key_t &key) {
    ctInitialized = true;
    ioc_lookup = true;
    iocInitialized = true;

    std::atomic<int> curThreadId = curMTSThread->getThreadId();
    Val_t val;
    int vs_id = 0;
    dc_entry_t *dc_entry;
    op_entry_t *op_entry;

#ifdef MTS_STATS_LATENCY
    uint64_t start, end;
    MTS_SET_TIMER(start);
#endif

    KeyIndex &keyindex = *g_perNumaKeyIndex[0];
    at_entry_t *at_entry = (at_entry_t *)keyindex.lookup(key);

    if((uintptr_t)at_entry == 0x0) {
	ts_trace(TS_ERROR, "[LOOKUP] keyindex.lookup returns non-exist key :%lu\n", key);
	return 0;
    }

    INC_GET_CNT();

    int val_pos;
RETRY_LOOKUP:
    val_pos = get_val_pos(at_entry, &vs_id);

    switch(val_pos) {
	case DCACHE_VAL:
	    {
		dc_entry = (dc_entry_t *)get_untagged_ptr((intptr_t)at_entry->val_addr);
		if(dc_entry == nullptr) goto RETRY_LOOKUP;
		if(dc_entry->at_entry != at_entry) {
		    ts_trace(TS_ERROR, "[LOOKUP] CANNOT ACCESS at_entry->dc_entry | at_entry: %p dc_entry: %p key: %lu\n", 
			    at_entry, dc_entry->at_entry, key);
		    goto RETRY_LOOKUP;
		}
		val = dc_entry->val;
		ts_trace(TS_INFO, "D lookup %lu val %lu %p\n", key, val, at_entry);
		INC_DCACHE_HIT_CNT();

#ifdef MTS_STATS_LATENCY
		MTS_SET_TIMER(end);
		add_timing_stat((end - start), val_pos);
#endif
		break;
	    }
	case OPLOG_VAL: 
	    {
		op_entry = (op_entry_t *)get_untagged_ptr((intptr_t)at_entry->val_addr);
		if(op_entry->opa != at_entry) {
		    ts_trace(TS_INFO, "[LOOKUP] CANNOT ACCESS at_entry->op_entry | at_entry: %p key: %lu\n", at_entry, key);
		    goto RETRY_LOOKUP;
		}
		val = op_entry->val;
		ts_trace(TS_INFO, "O lookup key %lu val %lu %p\n", key, val, at_entry);
		INC_OPLOG_HIT_CNT();

#ifdef MTS_STATS_LATENCY
		MTS_SET_TIMER(end);
		add_timing_stat((end - start), val_pos);
#endif
		break;
	    }
	case VALUESTORAGE_VAL: 
	    {
		ts_trace(TS_INFO, "V lookup vs_id %lu key %lu %p\n", vs_id, key, at_entry);
		ValueStorage *vs = g_perNumaValueStorage[vs_id];
#ifdef MTS_STATS_LATENCY
		at_entry->timestamp = start;
#endif

		int batched = 0;
		int ring_idx = 0;
		aio_thread_state_t *cur_th_state = th_state[curThreadId];
		batched = apply_ops(object_combiner[vs_id][ring_idx], cur_th_state, batching_io, at_entry, vs, ring_idx);
		return 0;
	    }
	default:
	    {
		ts_trace(TS_ERROR, "[LOOKUP] CANNOT FIND KEY | at_entry %p\n", at_entry);
		return 0;
	    }
    }
    return val;
}

uint64_t MTSImpl::scan(Key_t &startKey, int range, std::vector<Val_t> &vec_result) {
    ctInitialized = true;
    ioc_scan = true;
    iocInitialized = true;

    int start_idx = 0;
    int vs_id, val_pos;
    Val_t val;
    vec_result.reserve(R_QD);
    vec_result.clear();
    std::vector<at_entry_t *> vs_at_vec[MTS_VS_NUM];
    dc_entry_t *dc_entry;
    op_entry_t *op_entry;

    int curThreadId = curMTSThread->getThreadId();
    int ring_idx = curThreadId;

#ifdef MTS_STATS_LATENCY
    uint64_t start, end;
    MTS_SET_TIMER(start);
#endif

    KeyIndex &keyindex = *g_perNumaKeyIndex[0];

    std::vector<Val_t> results;
    results.reserve(range);
    range = keyindex.lookupRange(startKey, range, results);

    /* scanning SVC and PWB */
    for(int i = 0; i < range; i++) {
	at_entry_t *at_entry = (at_entry_t *)results[i];
	INC_GET_CNT();

RETRY_SCAN: 
	val_pos = get_val_pos(at_entry, &vs_id);

	switch(val_pos) {
	    case DCACHE_VAL: 
		{
		    dc_entry = (dc_entry_t *)get_untagged_ptr((intptr_t)at_entry->val_addr);
		    if(dc_entry == nullptr) goto RETRY_SCAN;
		    if(dc_entry->at_entry != at_entry) {
			ts_trace(TS_INFO, "[SCAN] CANNOT ACCESS at_entry->dc_entry | at_entry: %p key: %lu\n", 
				at_entry, dc_entry->key);
			goto RETRY_SCAN;
		    }
		    val = dc_entry->val;
		    INC_DCACHE_HIT_CNT();

		    break;
		}
	    case OPLOG_VAL: 
		{
		    op_entry = (op_entry_t *)get_untagged_ptr((intptr_t)at_entry->val_addr);
		    if(op_entry->opa != at_entry) {
			ts_trace(TS_INFO, "[SCAN] CANNOT ACCESS at_entry->op_entry | at_entry: %p key: %lu\n", 
				at_entry, op_entry->key);
			goto RETRY_SCAN;
		    }
		    val = op_entry->val;
		    INC_OPLOG_HIT_CNT();

		    break;
		}
	    case VALUESTORAGE_VAL:
		{
		    vs_at_vec[vs_id].push_back(at_entry);
		    break;
		}
	}

	if(val_pos == VALUESTORAGE_VAL)
	    continue;

	vec_result.push_back(val);
    }

    /* scanning valuestorage from #0 to #MTS_VS_NUM */
    /* the number of value from valuestorage */
    uint64_t sz;
    int batched = 0;
    for(vs_id = 0; vs_id < MTS_VS_NUM; vs_id++) {
	if(!vs_at_vec[vs_id].empty())
	    batched += vs_at_vec[vs_id].size();
	else continue;
	ValueStorage &valuestorage = *g_perNumaValueStorage[vs_id];
	while(valuestorage.pending_ios[ring_idx]) {}

#ifdef MTS_STATS_LATENCY
	scan_latency[ring_idx] = start;
#endif
	valuestorage.get_val_scan(&vs_at_vec[vs_id], ring_idx);
    }
    sz = vec_result.size() + batched;

#ifdef MTS_STATS_LATENCY
    if(batched == 0) {
	MTS_SET_TIMER(end);
	add_timing_stat(end - start, VALUESTORAGE_VAL);
    }
#endif

    ts_trace(TS_INFO, "%d %d sz %d\n", vec_result.size(), batched, sz);
    return sz;
}


bool MTSImpl::remove(Key_t &key) {
    int curThreadId = curMTSThread->getThreadId();
    bool ret;

    op_entry_t *op_entry = nullptr;
    at_entry_t *at_entry = nullptr;

    KeyIndex &keyindex = *g_perNumaKeyIndex[0];
    at_entry = (at_entry_t *)keyindex.lookup(key);

    /* Step 1. Search key and Get the at_entry_addr */
    OpLog &oplog = *g_perNumaOpLog[0];

    /* Step 2. op_entry = nullptr */
    int vs_id;
    int val_pos = get_val_pos(at_entry, &vs_id);

    if(val_pos == OPLOG_VAL)
	oplog.unlink_to_at(at_entry);
    else {
	ValueStorage &valuestorage = *g_perNumaValueStorage[vs_id];
	vs_idx_t *vs_idx = (vs_idx_t *)&at_entry->val_addr;
	int chunk_offset = vs_idx->vs_offset / MTS_VS_ENTRIES_PER_CHUNK;
	int entry_offset = vs_idx->vs_offset % MTS_VS_ENTRIES_PER_CHUNK;
	valuestorage.unlink_to_at(chunk_offset, entry_offset, at_entry);

	if(val_pos == DCACHE_VAL)
	    cache_free_kv_items(at_entry, curThreadId);
    }

    /* Step 3. Reset bitmap of at_entry */
    int at_id;
    AddressTable *addresstable;
    for(int i = 0; i < MTS_AT_NUM; i++) {
	addresstable = g_perNumaAddressTable[i];
	at_id = addresstable->get_at_id(at_entry);
	if(at_id > -1) break;
    }
    ret = addresstable->free(at_entry);

    /* Step 4. Remove key in KeyIndex */
    ret = keyindex.remove(key);

    return ret;
}

KeyIndex* MTSImpl::createKeyIndex() {
    return new KeyIndex;
}

OpLog* MTSImpl::createOpLog(const char *path, int ol_id) {
    return new OpLog(path, ol_id);
}

AddressTable* MTSImpl::createAddressTable(const char *path, int at_id) {
    return new AddressTable(path, at_id);
}

ValueStorage* MTSImpl::createValueStorage(const char *path, int vs_id) {
    return new ValueStorage(path, vs_id);
}

void MTSImpl::registerThread() {
    int threadId = numThreads.fetch_add(1);
    ts_trace(TS_INFO, "registerThread | threadId: %d\n", threadId);
    auto mt = new MTSThread(threadId);
    g_mutex_.lock();
    g_MTSThreadSet.insert(mt);
    curMTSThread = mt; 
    curMTSThread->phase = g_phase[threadId];
    g_mutex_.unlock();
    std::atomic_thread_fence(std::memory_order_acq_rel);

    KeyIndex &keyindex = *g_perNumaKeyIndex[0];
    keyindex.registerThread();

    clear_timing_stat();
}

void MTSImpl::unregisterThread() {
    if (curMTSThread == NULL) return;
    int threadId = curMTSThread->getThreadId();
    curMTSThread->phase--;
    g_phase[threadId] = curMTSThread->phase;
    /*
     * phase 2: LOAD WORKLOAD (INSERT-ONLY)
     * phase 1: WARM-UP CACHE
     * phase 0: RUN WORKLOAD
     */
    ts_trace(TS_INFO, "unregisterThread | threadId: %d %d\n", threadId, g_phase[threadId]);
    if(g_phase[threadId] == 2) {
    }

    if(g_phase[threadId] == 0) {
#ifdef MTS_STATS_GET
	total_get_cnt.fetch_add(curMTSThread->get_cnt);
	total_dcache_hit_cnt.fetch_add(curMTSThread->dcache_hit_cnt);
	total_oplog_hit_cnt.fetch_add(curMTSThread->oplog_hit_cnt);
	total_valuestorage_hit_cnt.fetch_add(curMTSThread->valuestorage_hit_cnt);
#endif
    } else {
	MTS_RESET_GET_COUNTERS();
	curMTSThread->resetGetCntInfo();
	batched_cnt = 0;
	batched_io = 0;

#ifdef MTS_STATS_WAF
	for(int i = 0; i < MTS_VS_NUM; i++)
	    g_perNumaValueStorage[i]->total_vs_write_count = 0;
#endif
    }

    numThreads.fetch_sub(1);
    curMTSThread->setfinish();

    KeyIndex &keyindex = *g_perNumaKeyIndex[0];
    keyindex.unregisterThread();
}

bool MTSImpl::recover(Key_t &startKey) {
    int curThreadId = curMTSThread->getThreadId();
    int vs_id;
    int at_id;
    int val_pos;
    volatile Key_t key = startKey;
    AddressTable *addresstable;

    KeyIndex &keyindex = *g_perNumaKeyIndex[0];
    at_entry_t *at_entry = (at_entry_t *)keyindex.lookup(key);
    //ts_trace(TS_ERROR, "%lu %p\n", key, at_entry);

    if(at_entry == nullptr) {
	//ts_trace(TS_ERROR, "[RECOVER] keyindex.lookup returns non-exist key:%lu \n", key);
	return false;
    }

    val_pos = get_val_pos(at_entry, &vs_id);
    switch(val_pos) {
	case OPLOG_VAL:
	    {
		break;
	    }
	case DCACHE_VAL:
	    {
		at_entry->val_addr = nullptr;
	    }
	case VALUESTORAGE_VAL:
	    {
		vs_idx_t vs_idx = at_entry->vs_idx;
		int vs_offset = vs_idx.vs_offset;
		int vs_chunk_offset = vs_offset / MTS_VS_ENTRIES_PER_CHUNK;
		int vs_entry_offset = vs_offset % MTS_VS_ENTRIES_PER_CHUNK;

		ValueStorage *vs = g_perNumaValueStorage[vs_idx.vs_id];
		vs->set_vs_bitmap_info(vs_chunk_offset, vs_entry_offset);

		break;
	    }
	default:
	    ts_trace(TS_INFO, "[RECOVER] CANNOT FIND KEY)\n");
    }
    return true;
}
