
//#include "./pcm/pcm-memory.cpp"
//#include "./pcm/pcm-numa.cpp"
#include "./papi_util.cpp"

#include "microbench.h"

#include <cstring>
#include <cctype>
#include <atomic>

//#define USE_TBB

#ifdef USE_TBB
#include "tbb/tbb.h"
#endif

typedef uint64_t keytype; /* default: uint64_t */
typedef std::less<uint64_t> keycomp;

static const uint64_t key_type=0;
static const uint64_t value_type=1; // 0 = random pointers, 1 = pointers to keys

extern bool hyperthreading;

// Whether we only perform insert
static bool insert_only = false;
static bool recovery_test = false;


#include "util.h"

//==============================================================
// LOAD
//==============================================================
inline void load(int wl, 
                 int kt, 
                 int index_type, 
                 std::vector<keytype> &init_keys, 
                 std::vector<keytype> &keys, 
                 std::vector<uint64_t> &values, 
                 std::vector<int> &ranges, 
		 std::vector<int> &ops) {

    std::string workload_dir;
    std::string init_file;
    std::string txn_file;

    workload_dir = "workloads";
    init_file = workload_dir + "/load.trace";

    if (kt == RAND_KEY && wl == WORKLOAD_A) {
	txn_file = workload_dir + "/txnsa_zipf.trace";
    } else if (kt == RAND_KEY && wl == WORKLOAD_B) {
	txn_file = workload_dir + "/txnsb_zipf.trace";
    } else if (kt == RAND_KEY && wl == WORKLOAD_C) {
	txn_file = workload_dir + "/txnsc_zipf.trace";
    } else if (kt == RAND_KEY && wl == WORKLOAD_D) {
	txn_file = workload_dir + "/txnsd_zipf.trace";
    } else if (kt == RAND_KEY && wl == WORKLOAD_E) {
	txn_file = workload_dir + "/txnse_zipf.trace";
    } else if (kt == RAND_KEY && wl == WORKLOAD_F) {
	txn_file = workload_dir + "/txnsf_zipf.trace";
    } else if (kt == MONO_KEY && wl == WORKLOAD_A) {
	txn_file = workload_dir + "/txnsa_unif.trace";
    } else if (kt == MONO_KEY && wl == WORKLOAD_B) {
	txn_file = workload_dir + "/txnsb_unif.trace";
    } else if (kt == MONO_KEY && wl == WORKLOAD_C) {
	txn_file = workload_dir + "/txnsc_unif.trace";
    } else if (kt == MONO_KEY && wl == WORKLOAD_D) {
	txn_file = workload_dir + "/txnsd_unif.trace";
    } else if (kt == MONO_KEY && wl == WORKLOAD_E) {
	txn_file = workload_dir + "/txnse_unif.trace";
    } else if (kt == MONO_KEY && wl == WORKLOAD_F) {
	txn_file = workload_dir + "/txnsf_unif.trace";
    } else {
	fprintf(stderr, "Unknown workload type or key type: %d, %d\n", wl, kt);
	exit(1);
    }
    std::ifstream infile_load(init_file);

    std::string op;
    keytype key;
    int range;

    std::string insert("INSERT");
    std::string read("READ");
    std::string update("UPDATE");
    std::string scan("SCAN");

    int count = 0;
    while ((count < INIT_LIMIT) && infile_load.good()) {
	infile_load >> op >> key;
	if (op.compare(insert) != 0) {
	    std::cout << "READING LOAD FILE FAIL!\n";
	    std::cout << "PLEASE CHECK INIT_LIMIT microbench.h\n" ;
	    return;
	}
	init_keys.push_back(key);
	count++;
    }

    size_t total_num_key = init_keys.size();
    fprintf(stderr, "Loaded %d keys\n", total_num_key);

    count = 0;
    uint64_t value = 0;
    void *base_ptr = malloc(8);
    uint64_t base = (uint64_t)(base_ptr);
    free(base_ptr);

    keytype *init_keys_data = init_keys.data();

    if (value_type == 0) {
	while (count < total_num_key) {
	    value = base + rand();
	    values.push_back(value);
	    count++;
	}
    }
    else {
	while (count < total_num_key) {
	    values.push_back(reinterpret_cast<uint64_t>(init_keys_data+count));
	    count++;
	}
    }

    // If we do not perform other transactions, we can skip txn file
    if(insert_only == true) {
	return;
    }

    if(recovery_test == true) {
	return;
    }


    // If we also execute transaction then open the 
    // transacton file here
    std::ifstream infile_txn(txn_file);

    count = 0;
    while ((count < LIMIT) && infile_txn.good()) {
	infile_txn >> op >> key;
	if (op.compare(insert) == 0) {
	    ops.push_back(OP_INSERT);
	    keys.push_back(key);
	    ranges.push_back(1);
	}
	else if (op.compare(read) == 0) {
	    ops.push_back(OP_READ);
	    keys.push_back(key);
	    ranges.push_back(1);
	}
	else if (op.compare(update) == 0) {
	    ops.push_back(OP_UPSERT);
	    keys.push_back(key);
	    ranges.push_back(1);
	}
	else if (op.compare(scan) == 0) {
	    infile_txn >> range;
	    ops.push_back(OP_SCAN);
	    keys.push_back(key);
	    ranges.push_back(range);
	}
	else {
	    std::cout << "UNRECOGNIZED CMD!\n";
	    return;
	}
	count++;
    }


    // Average and variation
    long avg = 0, var = 0;
    // If it is YSCB-E workload then we compute average and stdvar
    if(ranges.size() != 0) {
	for(int r : ranges) {
	    avg += r;
	}

	avg /= (long)ranges.size();

	for(int r : ranges) {
	    var += ((r - avg) * (r - avg));
	}

	var /= (long)ranges.size();

	fprintf(stderr, "YCSB-E scan Avg length: %ld; Variance: %ld\n",
		avg, var);
    }

}

//==============================================================
// EXEC
//==============================================================
inline void exec(int wl, 
                 int index_type, 
                 int num_thread,
                 std::vector<keytype> &init_keys, 
                 std::vector<keytype> &keys, 
                 std::vector<uint64_t> &values, 
                 std::vector<int> &ranges, 
                 std::vector<int> &ops) {

    double start_time = 0;
    double end_time = 0;
    double tput = 0;
    double elapsed_time = 0;

  Index<keytype, keycomp> *idx = getInstance<keytype, keycomp>(index_type, key_type);
  int count = (int)init_keys.size();

  //RECOVERY PHASE-------------------------------------------------------------------------------------
  auto func1 = [idx, &init_keys, num_thread, &values, index_type] \
	       (uint64_t thread_id, bool) {
		   size_t total_num_key = init_keys.size();
		   size_t key_per_thread = total_num_key / num_thread;
		   size_t start_index = key_per_thread * thread_id;
		   size_t end_index = start_index + key_per_thread;

		   threadinfo *ti = threadinfo::make(threadinfo::TI_MAIN, -1);

		   //declare_periodic_count;
		   for(size_t i = start_index;i < end_index;i++) {
		       idx->recover(init_keys[i], ti);
		       //periodic_count(1000, "load_thread_id %d %lu%%", thread_id, i*100LU/end_index);
		   }

		   return;
	       };

  if(recovery_test == true) {
      std::cout << "### RECOVERY ===============================================================" << "\n";
      start_time = get_now(); 
      StartThreads(idx, num_thread, func1, false);
      end_time = get_now();
      std::cout << std::fixed;
      elapsed_time = (end_time - start_time);
      tput = count / elapsed_time;

      std::cout << "RECOVERY elapsed_time " << elapsed_time << "\n";
      std::cout << "RECOVERY throughput " << tput << "\n";
      std::cout << "### Finished ================================================================" << "\n";

      delete idx;
      return;
  }
  sleep(1);
  //---------------------------------------------------------------------------------------------------

  //WRITE ONLY TEST--------------------------------------------------------------------------------------
  fprintf(stderr, "Populating %d keys using %d threads\n", count, num_thread);

  auto func2 = [idx, &init_keys, num_thread, &values, index_type] \
	       (uint64_t thread_id, bool) {
		   size_t total_num_key = init_keys.size();
		   size_t key_per_thread = total_num_key / num_thread;
		   size_t start_index = key_per_thread * thread_id;
		   size_t end_index = start_index + key_per_thread;

		   threadinfo *ti = threadinfo::make(threadinfo::TI_MAIN, -1);

		   //declare_periodic_count;
		   for(size_t i = start_index;i < end_index;i++) {
		       idx->insert(init_keys[i], values[i], ti);
		       //periodic_count(1000, "load_thread_id %d %lu%%", thread_id, i*100LU/end_index);
		   } 

		   return;
	       };

  start_time = get_now(); 
  StartThreads(idx, num_thread, func2, false);
  end_time = get_now();

  std::cout << std::fixed;
  tput = count / (end_time - start_time);
  elapsed_time = (end_time - start_time);

  std::cout << "YCSB_INSERT throughput " << tput << "\n";
  std::cout << "Elapsed_time " << elapsed_time << "\n";

  // If the workload only executes load phase then we return here
  if(insert_only == true) {
    delete idx;
    return;
  }
  sleep(1);
  //---------------------------------------------------------------------------------------------------

  //CACHE WARM-UP--------------------------------------------------------------------------------------
#if 1
  auto func3 = [idx, &init_keys, num_thread, &values, index_type] \
	       (uint64_t thread_id, bool) {
		   size_t total_num_key = init_keys.size();
		   size_t key_per_thread = total_num_key / num_thread;
		   size_t start_index = key_per_thread * thread_id;
		   size_t end_index = start_index + key_per_thread;

		   threadinfo *ti = threadinfo::make(threadinfo::TI_MAIN, -1);
		   int gc_counter = 0;
		   std::vector<uint64_t> v;
		   v.reserve(10);
		   for(size_t i = start_index;i < end_index;i++) {
		       v.clear();
		       idx->find(init_keys[i], &v, ti);
		   } 
		   return;
	       };
  StartThreads(idx, num_thread, func3, false);
  sleep(1);
#endif
  //---------------------------------------------------------------------------------------------------

  //READ/UPDATE/SCAN TEST------------------------------------------------------------------------------
  int txn_num = GetTxnCount(ops, index_type);
  uint64_t sum = 0;
  uint64_t s = 0;

  if(values.size() < keys.size()) {
    fprintf(stderr, "Values array too small\n");
    exit(1);
  }

  fprintf(stderr, "# of Txn: %d\n", txn_num);
  
  auto func4 = [num_thread, 
                idx, index_type, 
                //&read_miss_counter,
                //&read_hit_counter,
                &keys,
                &values,
                &ranges,
                &ops](uint64_t thread_id, bool) {
    size_t total_num_op = ops.size();
    size_t op_per_thread = total_num_op / num_thread;
    size_t start_index = op_per_thread * thread_id;
    size_t end_index = start_index + op_per_thread;
    size_t current_time = get_now();
   
    std::vector<uint64_t> v;
    v.reserve(10);
 
    threadinfo *ti = threadinfo::make(threadinfo::TI_MAIN, -1);
    int counter = 0;
    size_t temp_time = get_now();
    size_t current_thp = 0;
    int op_cnt= 0;

    //declare_periodic_count;
    for(size_t i = start_index;i < end_index;i++) {
	int op = ops[i];

	if (op == OP_INSERT) { //INSERT
	    idx->insert(keys[i], values[i], ti);
	}
	else if (op == OP_READ) { //READ
	    v.clear();
	    idx->find(keys[i], &v, ti);
	}
	else if (op == OP_UPSERT) { //UPDATE
	    idx->upsert(keys[i], reinterpret_cast<uint64_t>(&keys[i]), ti);
	}
	else if (op == OP_SCAN) { //SCAN
	    idx->scan(keys[i], ranges[i], ti);
	}

	//periodic_count(1000, "thread_id %d", thread_id);
    }
    return;
  };

  start_time = get_now();  
  StartThreads(idx, num_thread, func4, false);
  end_time = get_now();

  tput = txn_num / (end_time - start_time);
  elapsed_time = end_time - start_time;
  //---------------------------------------------------------------------------------------------------

  if (wl == WORKLOAD_A) {  
      std::cout << "YCSB_A throughput " << (tput + (sum - sum)) << std::endl;
      std::cout << "Elapsed_time " << elapsed_time;
  } else if (wl == WORKLOAD_B) {  
      std::cout << "YCSB_B throughput " << (tput + (sum - sum)) << std::endl;
      std::cout << "Elapsed_time " << elapsed_time;
  } else if (wl == WORKLOAD_C) {
      std::cout << "YCSB_C throughput " << (tput + (sum - sum)) << std::endl;
      std::cout << "Elapsed_time " << elapsed_time;
  } else if (wl == WORKLOAD_D) {  
      std::cout << "YCSB_D throughput " << (tput + (sum - sum)) << std::endl;
      std::cout << "Elapsed_time " << elapsed_time;
  } else if (wl == WORKLOAD_E) {
      std::cout << "YCSB_E throughput " << (tput + (sum - sum)) << std::endl;
      std::cout << "Elapsed_time " << elapsed_time;
  } else if (wl == WORKLOAD_F) {  
      std::cout << "YCSB_F throughput " << (tput + (sum - sum)) << std::endl;
      std::cout << "Elapsed_time " << elapsed_time;
  }  else {
      fprintf(stderr, "Unknown workload type: %d\n", wl);
      exit(1);
  }

  std::cout << "\n";

  delete idx;

  return;
}

int main(int argc, char *argv[]) {

  if (argc < 3) {
    std::cout << "Usage:\n";
    std::cout << "1. workload type: a, b, c, d, e, none\n";
    std::cout << "   \"none\" type means we just load the file and exit. \n"
                 "This serves as the base line for microbenchamrks\n";
    std::cout << "2. key distribution: zipf, unif\n";
    std::cout << "3. number of threads (integer)\n";
    std::cout << "   --insert-only: Whether to only execute insert operations\n";
    std::cout << "   --recovery-test: Whether to only execute recovery operations\n";

    
    return 1;
  }

  // Then read the workload type
  int wl;
  if (strcmp(argv[1], "a") == 0) {
    wl = WORKLOAD_A;
  } else if (strcmp(argv[1], "b") == 0) {
    wl = WORKLOAD_B;
  } else if (strcmp(argv[1], "c") == 0) {
    wl = WORKLOAD_C;
  } else if (strcmp(argv[1], "e") == 0) {
    wl = WORKLOAD_E;
  } else if (strcmp(argv[1], "d") == 0) {
    wl = WORKLOAD_D;
  } else if (strcmp(argv[1], "f") == 0) {
    wl = WORKLOAD_F;
  } else {
    fprintf(stderr, "Unknown workload: %s\n", argv[1]);
    exit(1);
  }

  // Then read key type
  int kt;
  if (strcmp(argv[2], "zipf") == 0) {
    kt = RAND_KEY;
  } else if (strcmp(argv[2], "unif") == 0) {
    kt = MONO_KEY;
  } else {
    fprintf(stderr, "Unknown key type: %s\n", argv[2]);
    exit(1);
  }

  int index_type;
  index_type = TYPE_MTS;

  // Then read number of threads using command line
  int num_thread = atoi(argv[3]);
  if(num_thread < 1 || num_thread > 500) {
    fprintf(stderr, "Do not support %d threads\n", num_thread);
    exit(1);
  } else {
    fprintf(stderr, "### Loading workload file ==================================================\n");
    fprintf(stderr, "Number of threads: %d\n", num_thread);
  }
  
  // Then read all remianing arguments
  int repeat_counter = 1;
  char **argv_end = argv + argc;
  for(char **v = argv + 4;v != argv_end;v++) {
      if(strcmp(*v, "--insert-only") == 0) {
	  insert_only = true;
      } else if(strcmp(*v, "--recovery") == 0) {
	  recovery_test = true;
      } else if(strcmp(*v, "--repeat") == 0) {
	  // If we repeat, then exec() will be called for 5 times
	  repeat_counter = 5;
      } else {
	  fprintf(stderr, "Unknown switch: %s\n", *v);
	  exit(1);
      }
  }

  if(repeat_counter != 1) {
    fprintf(stderr, "  Repeat for %d times (NOTE: Memory number may not be correct)\n",
            repeat_counter);
  }

  if(insert_only == true) {
      fprintf(stderr, "Program will exit after insert operation\n");
  }

  if(recovery_test == true) {
      fprintf(stderr, "Program will exit after recovery operation\n");
  }

  std::vector<keytype> init_keys;
  std::vector<keytype> keys;
  std::vector<uint64_t> values;
  std::vector<int> ranges;
  std::vector<int> ops; //INSERT = 0, READ = 1, UPDATE = 2

  init_keys.reserve(100000000);
  keys.reserve(100000000);
  values.reserve(100000000);
  ranges.reserve(100000000);
  ops.reserve(100000000);

  memset(&init_keys[0], 0x00, 100000000 * sizeof(keytype));
  memset(&keys[0], 0x00, 100000000 * sizeof(keytype));
  memset(&values[0], 0x00, 100000000 * sizeof(uint64_t));
  memset(&ranges[0], 0x00, 100000000 * sizeof(int));
  memset(&ops[0], 0x00, 100000000 * sizeof(int));

  load(wl, kt, index_type, init_keys, keys, values, ranges, ops);
  //printf("Finished loading workload file\n");
  if(index_type != TYPE_NONE) {
      // Then repeat executing the same workload
      while(repeat_counter > 0) {
	  exec(wl, index_type, num_thread, init_keys, keys, values, ranges, ops);
	  repeat_counter--;
	  //printf("Finished running benchmark\n");
      }
  } else {
      fprintf(stderr, "Type None is selected - no execution phase\n");
  }

  //exit_cleanup();

  return 0;
}
