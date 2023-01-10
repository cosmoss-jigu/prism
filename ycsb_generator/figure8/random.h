#ifndef RANDOM_H
#define RANDOM_H
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

typedef long (*random_gen_t)(void);

unsigned long xorshf96(void);
unsigned long locxorshf96(void);

void init_seed(void); // must be called after each thread creation
void init_zipf_generator(long min, long max);
long zipf_next(); // zipf distribution, call init_zipf_generator first
long uniform_next(); // uniform, call init_zipf_generator first
long bogus_rand(); // returns something between 1 and 1000
long production_random1(void); // production workload simulator
long production_random2(void); // production workload simulator

double zeta(long st, long n, double initialsum);
double zetastatic(long st, long n, double initialsum);
long next_long(long itemcount);
long zipf_next();

void shuffle(size_t *array, size_t n);


void init_latestgen(long init_val);
long next_value_latestgen();

const char *get_function_name(random_gen_t f);
#endif
