// SPDX-FileCopyrightText: Copyright (c) 2019-2021 Virginia Tech
// SPDX-License-Identifier: Apache-2.0

#include "pactree.h"
#include <numa-config.h>

#define NUMDATA 10000

int main(int argc, char **argv){
    pactree *pt = new pactree(1);
    pt->registerThread();

    std::vector<Val_t> result;
    result.reserve(10);
    for(int i = 1; i < NUMDATA; i++) {
	pt->insert(i,i);
	printf("%d\n", i);
    }

    pt->scan(1, 10, result);

    for(int i = 1; i < NUMDATA; i++) {
	if(i!=pt->lookup(i)){
		printf("error");
		exit(1);
	}
    }
}
