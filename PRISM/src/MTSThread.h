#ifndef MTS_THREADS_H
#define MTS_THREADS_H
#include "common.h"
#include <atomic>

class MTSThread {
    private:
	int threadId;
	uint64_t localClock;
	bool finish;
	volatile std::atomic<uint64_t> runCnt;
	int valuestorageId;
    public:
	MTSThread(int threadId) {
	    this->threadId = threadId;
	    this->finish = false;
	    this->runCnt = 0;

	    this->valuestorageId = -1;
#ifdef MTS_STATS_GET
	    get_cnt = 0;
	    dcache_hit_cnt = 0;
	    oplog_hit_cnt = 0;
	    valuestorage_hit_cnt = 0;
#endif
#ifdef MTS_STATS_WAF
	    ol_write_count = 0;
#endif
	}
	int phase;
#ifdef MTS_STATS_GET
	uint64_t get_cnt;
	uint64_t dcache_hit_cnt;
	uint64_t oplog_hit_cnt;
	uint64_t valuestorage_hit_cnt;

	void resetGetCntInfo() {
	    get_cnt = 0;
	    dcache_hit_cnt = 0;
	    oplog_hit_cnt = 0;
	    valuestorage_hit_cnt = 0;
	}
#else
	void resetGetCntInfo() {}
#endif
#ifdef MTS_STATS_WAF
	uint64_t ol_write_count;
#endif
	void setThreadId (int threadId) {this->threadId = threadId;}
	int getThreadId ()  {return this->threadId;}
	void setfinish() {this->finish = true;}
	bool getFinish() {return this->finish;}
	void setLocalClock (uint64_t clock) {this->localClock = clock;}
	uint64_t getLocalClock() {return this->localClock;}
	void incrementRunCntAtomic() { runCnt.fetch_add(1);};
	void incrementRunCnt() { runCnt++;};
	uint64_t getRunCnt() {return this->runCnt;}
	void read_lock(uint64_t clock) {
	    this->setLocalClock(clock);
	    this->incrementRunCntAtomic();
	}
	void read_unlock() {
	    this->incrementRunCnt();
	}

	void setValueStorageId(int id) {this->valuestorageId = id;};
	int getValueStorageId() {return this->valuestorageId;};
};

#endif //MTS_THREADS_H
