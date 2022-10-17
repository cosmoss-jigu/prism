#include "common.h"
#include "numa.h"
#include "../lib/TSOpLog/debug.h"

#include "../lib/pactree/include/pactree.h"

class PACTREEIndex {
    private:
	int numa;
	pactree *idx;
    public:
	PACTREEIndex() {
	    idx = new pactree(1);
	}
	~PACTREEIndex() {
	    delete idx;
	}

	void registerThread() {
	    idx->registerThread();
	}

	void unregisterThread() {
	    idx->unregisterThread();
	}

	bool insert(Key_t key, void* ptr) {
	    idx->insert(key, reinterpret_cast<Val_t>(ptr));
	    return true;
	}
	bool remove(Key_t key) {
	    idx->remove(key);
	    return true;
	}
	void* lookup(Key_t key) {
	    auto result = idx->lookup(key);
	    return reinterpret_cast<void*>(result);
	}
	size_t lookupRange(Key_t start, int range, std::vector<Val_t> &results) {
	    auto resultCount = idx->scan(start, range, results);
	    return resultCount;
	}
};

typedef PACTREEIndex KeyIndex;

