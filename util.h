
#include "indexkey.h" 
#include "microbench.h"
#include "index.h"
#include "numa-config.h"

#ifndef _UTIL_H
#define _UTIL_H

bool hyperthreading = false;

//This enum enumerates index types we support
enum {
  TYPE_MTS,
  TYPE_NONE,
};

// These are workload operations
enum {
  OP_INSERT,
  OP_READ,
  OP_UPSERT,
  OP_SCAN,
};

// These are YCSB workloads
enum {
  WORKLOAD_A,
  WORKLOAD_B,
  WORKLOAD_C,
  WORKLOAD_E,
  WORKLOAD_F,
  WORKLOAD_D,
};

// These are key types we use for running the benchmark
enum {
  RAND_KEY,
  MONO_KEY,
  RDTSC_KEY,
  EMAIL_KEY,
};

//==============================================================
// GET INSTANCE
//==============================================================
template<typename KeyType, 
         typename KeyComparator=std::less<KeyType>, 
         typename KeyEuqal=std::equal_to<KeyType>, 
         typename KeyHash=std::hash<KeyType>>
Index<KeyType, KeyComparator> *getInstance(const int type, const uint64_t kt) {
  if (type == TYPE_MTS)
    return new MTSIndex<KeyType, KeyComparator>(kt);
  else {
    fprintf(stderr, "Unknown index type: %d\n", type);
    exit(1);
  }
  
  return nullptr;
}

inline double get_now() { 
struct timeval tv; 
  gettimeofday(&tv, 0); 
  return tv.tv_sec + tv.tv_usec / 1000000.0; 
} 

/*
 * Rdtsc() - This function returns the value of the time stamp counter
 *           on the current core
 */
inline uint64_t Rdtsc()
{
    uint32_t lo, hi;
    asm volatile("rdtsc" : "=a" (lo), "=d" (hi));
    return (((uint64_t) hi << 32) | lo);
}

// This is the order of allocation

static int core_alloc_map_hyper[] = {
  0, 2, 4, 6, 8, 10, 12, 14, 16, 18,
  20, 22, 24, 26, 28, 30, 32, 34, 36, 38,
  1, 3, 5, 7 ,9, 11, 13, 15, 17, 19,
  21, 23, 25, 27, 29, 31, 33, 35, 37, 39,  
};


constexpr static size_t MAX_CORE_NUM = NUM_PHYSICAL_CPU_PER_SOCKET * NUM_SOCKET * SMT_LEVEL;
// 40 = 20 * 2 * 1
inline void PinToCore(size_t thread_id, uint64_t num_threads) {
    cpu_set_t cpu_set;
    CPU_ZERO(&cpu_set);

    int socket, smt, physical_cpu, temp;
    if(thread_id < num_threads/NUM_SOCKET) {
	socket = 0;
	physical_cpu = thread_id % NUM_PHYSICAL_CPU_PER_SOCKET;
	if(thread_id < NUM_PHYSICAL_CPU_PER_SOCKET)
	    smt = 0;
	else smt = 1;
    } else {
	socket = 1;
	temp = thread_id;
	temp -= num_threads/NUM_SOCKET;
	physical_cpu = temp % NUM_PHYSICAL_CPU_PER_SOCKET;
	if(thread_id < NUM_PHYSICAL_CPU_PER_SOCKET)
	    smt = 0;
	else smt = 1;
    }
    CPU_SET(OS_CPU_ID[socket][physical_cpu][smt], &cpu_set);

    int ret = pthread_setaffinity_np(pthread_self(), sizeof(cpu_set), &cpu_set);
    if(ret != 0) {
	fprintf(stderr, "%d PinToCore() returns non-0\n", thread_id);
	exit(1);
    }
    return;
}

template <typename Fn, typename... Args>
void StartThreads(Index<keytype, keycomp> *tree_p,
                  uint64_t num_threads,
                  Fn &&fn,
                  Args &&...args) {
  std::vector<std::thread> thread_group;

  if(tree_p != nullptr) {
    tree_p->UpdateThreadLocal(num_threads);
  }

  auto fn2 = [tree_p, &fn](uint64_t num_threads, uint64_t thread_id, Args ...args) {
    if(tree_p != nullptr) {
      tree_p->AssignGCID(thread_id);
    }

    PinToCore(thread_id, num_threads);
    fn(thread_id, args...);

    if(tree_p != nullptr) {
      tree_p->UnregisterThread(thread_id);
    }

    return;
  };

  for (uint64_t thread_itr = 0; thread_itr < num_threads; ++thread_itr) {
    thread_group.push_back(std::thread{fn2, num_threads, thread_itr, std::ref(args...)});
  }

  for (uint64_t thread_itr = 0; thread_itr < num_threads; ++thread_itr) {
    thread_group[thread_itr].join();
  }

  if(tree_p != nullptr) {
    tree_p->UpdateThreadLocal(1);
  }

  return;
}

/*
 * GetTxnCount() - Counts transactions and return 
 */
template <bool upsert_hack=true>
int GetTxnCount(const std::vector<int> &ops,
                int index_type) {
  int count = 0;
 
  for(auto op : ops) {
    switch(op) {
      case OP_INSERT:
      case OP_READ:
      case OP_SCAN:
        count++;
        break;
      case OP_UPSERT:
        count++;

        break;
      default:
        fprintf(stderr, "Unknown operation\n");
        exit(1);
        break;
    }
  }

  return count;
}

#ifdef __x86_64__
#define rdtscll(val) {                                           \
       unsigned int __a,__d;                                        \
       asm volatile("rdtsc" : "=a" (__a), "=d" (__d));              \
       (val) = ((unsigned long)__a) | (((unsigned long)__d)<<32);   \
}
#else
#define rdtscll(val) __asm__ __volatile__("rdtsc" : "=A" (val))
#endif

/*
 * Cute timer macros
 * Usage:
 * declare_timer;
 * start_timer {
 *   ...
 * } stop_timer("Took %lu us", elapsed);
 */
#define declare_timer uint64_t elapsed; \
   struct timeval st, et;

#define start_timer gettimeofday(&st,NULL);

#define stop_timer(msg, args...) ;do { \
   gettimeofday(&et,NULL); \
   elapsed = ((et.tv_sec - st.tv_sec) * 1000000) + (et.tv_usec - st.tv_usec) + 1; \
   printf("(%s,%d) [%6lums] " msg "\n", __FUNCTION__ , __LINE__, elapsed/1000, ##args); \
} while(0)



/*
 * Cute way to print a message periodically in a loop.
 * declare_periodic_count;
 * for(...) {
 *    periodic_count(1000, "Hello"); // prints hello and the number of iterations every second
 * }
 */
#define declare_periodic_count \
      uint64_t __real_start = 0, __start, __last, __nb_count; \
      if(!__real_start) { \
         rdtscll(__real_start); \
         __start = __real_start; \
         __nb_count = 0; \
      }

//printf("(%s,%d) [%3lus] [%7lu ops/s] " msg "\n", __FUNCTION__ , __LINE__, cycles_to_us(__last - __real_start)/1000000LU, __nb_count*1000000LU/cycles_to_us(__last - __start), ##args);
#define periodic_count(period, msg, args...) \
   do { \
      rdtscll(__last); \
      __nb_count++; \
      if(cycles_to_us(__last - __start) > ((period)*1000LU)) { \
	  printf("%3lus %7lu ops/s " msg "\n", cycles_to_us(__last - __real_start)/1000000LU, __nb_count*1000000LU/cycles_to_us(__last - __start), ##args); \
         __nb_count = 0; \
         __start = __last; \
      } \
   } while(0);


#endif
