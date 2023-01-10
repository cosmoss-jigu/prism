#include "util.h"
#include "numa-config.h"
#include <sys/time.h>

#define MAX_STATS 10000000UL

void cache_prefetch(char *addr, int size, int locality) {
    int iter = size / L1_CACHE_BYTES;

    switch(locality) {
	case 2:
	    for(int i = 0; i < iter; i++) {
		addr += L1_CACHE_BYTES * i;
		cache_prefetchr_high(addr);
	    }
	    break;
	case 1:
	    for(int i = 0; i < iter; i++) {
		addr += L1_CACHE_BYTES * i;
		cache_prefetchr_high(addr);
	    }
	    break;
	case 0:
	    for(int i = 0; i < iter; i++) {
		addr += L1_CACHE_BYTES * i;
		cache_prefetchr_high(addr);
	    }
	    break;
    }
}

static uint64_t freq = 0;
static uint64_t get_cpu_freq(void) {
    if(freq)
	return freq;

    FILE *fd;
    float freqf = 0;
    char *line = NULL;
    size_t len = 0;

    fd = fopen("/proc/cpuinfo", "r");
    if (!fd) {
	fprintf(stderr, "failed to get cpu frequency\n");
	perror(NULL);
	return freq;
    }

    while (getline(&line, &len, fd) != EOF) {
	if (sscanf(line, "cpu MHz\t: %f", &freqf) == 1) {
	    freqf = freqf * 1000000UL;
	    freq = (uint64_t) freqf;
	    break;
	}
    }

    fclose(fd);
    return freq;
}

uint64_t cycles_to_ns(uint64_t cycles) {
    double temp = (double)cycles/get_cpu_freq();
    return temp*1000000000LU;
}

uint64_t cycles_to_us(uint64_t cycles) {
    double temp = (double)cycles/get_cpu_freq();
    return temp*1000000LU;
}

unsigned long tacc_rdtscp(int *chip, int *core) {
    unsigned long a,d,c;
    __asm__ volatile("rdtscp" : "=a" (a), "=d" (d), "=c" (c));
    *chip = (c & 0xFFF000)>>12;
    *core = c & 0xFFF;
    return ((unsigned long)a) | (((unsigned long)d) << 32);;
}

constexpr static size_t MAX_CORE_NUM = NUM_PHYSICAL_CPU_PER_SOCKET * NUM_SOCKET * SMT_LEVEL;
void pin_thread(std::thread *t, int thread_id, int num_threads) {
    cpu_set_t cpu_set;
    CPU_ZERO(&cpu_set);

    int socket, physical_cpu, smt, temp;

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

    ts_trace(TS_INFO, "MTS | num_threads: %d thread_id: %d socket: %d physical_cpu: %d smt: %d\n",
	    num_threads, thread_id, socket, physical_cpu, smt); 
    int ret = pthread_setaffinity_np(t->native_handle(), sizeof(cpu_set), &cpu_set);
    if(ret != 0) {
	fprintf(stderr, "%d PinToCore() returns non-0\n", thread_id);
	exit(1);
    }

    return;
}

void pin_thread(std::thread *t, int socket, int physical_cpu, int smt) {
    cpu_set_t cpu_set;
    CPU_ZERO(&cpu_set);

    CPU_SET(OS_CPU_ID[socket][physical_cpu][smt], &cpu_set);

    ts_trace(TS_ERROR, "MTS | socket: %d physical_cpu: %d smt: %d\n", socket, physical_cpu, smt); 
    int ret = pthread_setaffinity_np(t->native_handle(), sizeof(cpu_set), &cpu_set);
    if(ret != 0) {
	fprintf(stderr, "socket %d p-cpu %d PinToCore() returns non-0\n", socket, physical_cpu);
	exit(1);
    }

    return;
}

// STATS //////////////////////////////////////////////////////////////////////////////////////////
struct stats {
    uint64_t *timing_time;
    uint64_t *timing_value;
    size_t timing_idx;
    size_t max_timing_idx;
} stats;

int cmp_uint(const void *_a, const void *_b) {
    uint64_t a = *(uint64_t*)_a;
    uint64_t b = *(uint64_t*)_b;
    if(a > b)
	return 1;
    else if(a < b)
	return -1;
    else
	return 0;
}

void clear_timing_stat() {
    stats.timing_idx = 0;
}

void add_timing_stat(uint64_t elapsed, uint64_t location) {
    if(!stats.timing_value) {
	int lat_stats = MAX_STATS;
	stats.timing_value = (uint64_t *)malloc(lat_stats * sizeof(*stats.timing_value));
	memset(stats.timing_value, 0, lat_stats * sizeof(*stats.timing_value));
	stats.timing_idx = 0;
	stats.max_timing_idx = lat_stats;
    }
    if(stats.timing_idx >= stats.max_timing_idx) {
	stats.timing_idx = 0;
    }
    stats.timing_value[stats.timing_idx] = elapsed;
    stats.timing_idx++;
}

void print_stats() {
    uint64_t avg = 0;

    if(stats.timing_idx == 0) {
	printf("#No stat has been collected\n");
	return;
    }

    int lat_stats = MAX_STATS;
    uint64_t last = lat_stats;

    /* for debugging */
    int j = 0;
    for(int i = 0; i < last; i++) {
	if(stats.timing_value[i] > 0) {
	    j++;
	}     
    }

    last = j;
    for(uint64_t i = 1; i < last; i++) {
	avg += stats.timing_value[i];
    }

    qsort(stats.timing_value, last, sizeof(*stats.timing_value), cmp_uint);
    printf("AVG %lu\n", cycles_to_us(avg/last));
    printf("50p %lu\n", cycles_to_us(stats.timing_value[last*50/100]));
    printf("90p %lu\n", cycles_to_us(stats.timing_value[last*90/100]));
    printf("95p %lu\n", cycles_to_us(stats.timing_value[last*95/100]));
    printf("99p %lu\n", cycles_to_us(stats.timing_value[last*99/100]));

    stats.timing_idx = 0;
} 

uint64_t mts_get_now() {
    struct timeval tv;
    gettimeofday(&tv, 0);
    return tv.tv_sec + tv.tv_usec / 1000000.0;
}

intptr_t put_tagged_ptr(intptr_t entry, unsigned long long tag) {
    const intptr_t mask = ~(1ULL << 48);
    intptr_t untagged_entry = get_untagged_ptr(entry);
    intptr_t p = ((untagged_entry & mask) | (tag << 56));
    
    return p;
}

intptr_t get_untagged_ptr(intptr_t entry) {
    intptr_t p = (entry << 16) >> 16;

    return p;
}

int get_tag(intptr_t entry) {
    intptr_t p = entry;
    int tag = p >> 56;

    return tag;
}
