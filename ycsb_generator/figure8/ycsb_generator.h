#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    WORKLOAD_A,
    WORKLOAD_B,
    WORKLOAD_C,
    WORKLOAD_D,
    WORKLOAD_E,
    WORKLOAD_F,
    WORKLOAD_W,
};

enum {
    MONO_KEY,
    RAND_KEY,
};


static int random_get_put(int test);
void create_workload_abc(int type, int zipfian, size_t items_num);
void create_workload_d(int type, int zipfian, size_t items_num);
void create_workload_e(int type, int zipfian, size_t items_num);
void create_workload_f(int type, int zipfian, size_t items_num);
