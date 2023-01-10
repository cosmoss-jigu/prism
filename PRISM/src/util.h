#ifndef MTS_UTIL_H
#define MTS_UTIL_H

#include "MTSImpl.h"
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define PAUSE() asm volatile("pause");
#define NOP10() asm("nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;");

void cache_prefetch(void *addr, int size, int locality);
static uint64_t get_cpu_freq(void);
uint64_t cycles_to_ns(uint64_t cycles);
uint64_t cycles_to_us(uint64_t cycles);
unsigned long tacc_rdtscp(int *chip, int *core);
void pin_thread(std::thread *t, int thread_id, int num_threads);
void pin_thread(std::thread *t, int socket, int physical_cpu, int smp);
void add_timing_stat(uint64_t elapsed, uint64_t location);
void print_stats();
void clear_timing_stat();
uint64_t mts_get_now();

intptr_t put_tagged_ptr(intptr_t at_entry, unsigned long long tag);
intptr_t get_untagged_ptr(intptr_t at_entry);
int get_tag(intptr_t at_entry);

#endif /* MTS_UTILS_H */
