#ifndef MTS_COMMON_H
#define MTS_COMMON_H
#include <cstdint>
#include "../lib/TSOpLog/debug.h"
#include "../lib/TSOpLog/nvm.h"

typedef uint64_t Key_t;
typedef uint64_t Val_t;

class OpForm {
    public:
	enum Operation {INSERT, REMOVE, INVALID, LOOKUP, SCAN};
	Operation op_type;
	Key_t key;
	Val_t val;
	void *at_entry;
	uint64_t ts;
	bool operator< (const OpForm& ops) const {
	    return (ts < ops.ts);
	}
};

#endif //MTS_COMMON_H
