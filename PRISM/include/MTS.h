#ifndef MTS_MTSAPI_H
#define MTS_MTSAPI_H

#include "MTSImpl.h"
#include "common.h"

class MTS {
    private:
	MTSImpl *mts;
    public:
	MTS(int numa) {
	    mts = new MTSImpl(numa);
	}
	~MTS() {
	    delete mts;
	}
	bool insert(Key_t key, Val_t val) {
	    return mts->insert(key, val);
	}
	bool update(Key_t key, Val_t val) {
	    return mts->update(key, val);
	}
	Val_t lookup(Key_t key) {
	    return mts->lookup(key);
	}
	bool remove(Key_t key) {
	    return mts->remove(key);
	}
	uint64_t scan(Key_t startKey, int range, std::vector<Val_t> &result) {
	    return mts->scan(startKey, range, result);
	}
	bool recover(Key_t startKey) {
	    return mts->recover(startKey);
	}
	void registerThread() {
	    mts->registerThread();
	}
	void unregisterThread() {
	    mts->unregisterThread();
	}
};

#endif //MTS_MTSAPI_H
