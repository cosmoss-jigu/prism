#include "ycsb_generator.h"
#include "random.h"

static int random_get_put(int test) {
    long random = uniform_next() % 100;
    switch(test) {
	case 0: // A
	    return random >= 50; 
	case 1: // B
	    return random >= 95; 
	case 2: // C
	    return 0;
	case 3: // D
	    return random >= 95; 
	case 4: // E
	    return random >= 95; 
	case 5: // F
	    return random >= 100;
    }   

    printf("Not a valid test\n");
    exit(1);
}

void create_workload_load(int type, size_t items_num) {
    FILE *fp;
    size_t *pos = NULL;
    pos = malloc(items_num * sizeof(*pos));

    for(size_t i = 0; i < items_num; i++)
	pos[i] = i + 1;

    shuffle(pos, items_num);

    char *file_name = malloc(sizeof(char) * 20);
    file_name = "load.trace";
    remove(file_name);
    fp = fopen(file_name, "a+");

    for(size_t i = 0; i < items_num; i++) {
	fprintf(fp, "INSERT %lu\n", pos[i]);
    }

    fclose(fp);
}

void create_workload_abc(int type, int zipfian, size_t items_num) {
    items_num = items_num / 4; // only for figure 8

    if(type > 2) {
	printf("wrong wkld type %d\n", type);
	exit(1);
    }
    
    size_t item;
    FILE *fp;

    char *dist_name = malloc(sizeof(char) * 10);
    if(zipfian) 
	dist_name = "_zipf.trace";
    else 
	dist_name = "_unif.trace";

    char *type_name = malloc(sizeof(char) * 10);
    switch(type) {
	case WORKLOAD_A:
	    type_name = "a";
	    break;
	case WORKLOAD_B:
	    type_name = "b";
	    break;
	case WORKLOAD_C:
	    type_name = "c";
    }

    char *file_name = malloc(sizeof(char) * 100);
    strcpy(file_name, "txns");
    strcat(file_name, type_name);
    strcat(file_name, dist_name);

    remove(file_name);
    fp = fopen(file_name, "a+");

    for(size_t i = 0; i < items_num; i++) {
	if(zipfian)
	    item = zipf_next();
	else {
	    item = uniform_next();
	}

	if(item == 0)
	    item++;

	if(random_get_put(type))
	    fprintf(fp, "UPDATE %lu\n", item);
	else
	    fprintf(fp, "READ %lu\n", item);
    }

    fclose(fp);
}

void create_workload_d(int type, int zipfian, size_t items_num) {
    size_t item;
    FILE *fp;

    items_num = items_num / 4; // only for figure 8

    char *dist_name = malloc(sizeof(char) * 10);
    if(zipfian) 
	dist_name = "_zipf.trace";
    else 
	dist_name = "_unif.trace";

    char *type_name = malloc(sizeof(char) * 10);
    type_name = "d";

    char *file_name = malloc(sizeof(char) * 100);
    strcpy(file_name, "txns");
    strcat(file_name, type_name);
    strcat(file_name, dist_name);

    remove(file_name);
    fp = fopen(file_name, "a+");

    for(size_t i = 0; i < items_num; i++) {
	if(zipfian)
	    item = next_value_latestgen();
	else
	    item = uniform_next();

	if(item == 0)
	    item++;

	if(random_get_put(type))
	    fprintf(fp, "UPDATE %lu\n", item);
	else
	    fprintf(fp, "READ %lu\n", item);
    }

    fclose(fp);
}

void create_workload_e(int type, int zipfian, size_t items_num) {
    items_num = items_num / 4;

    size_t item;
    FILE *fp;

    char *dist_name = malloc(sizeof(char) * 10);
    if(zipfian) 
	dist_name = "_zipf.trace";
    else 
	dist_name = "_unif.trace";

    char *type_name = malloc(sizeof(char) * 10);
    type_name = "e";

    char *file_name = malloc(sizeof(char) * 100);
    strcpy(file_name, "txns");
    strcat(file_name, type_name);
    strcat(file_name, dist_name);

    remove(file_name);
    fp = fopen(file_name, "a+");

    random_gen_t rand_next = zipfian ? zipf_next:uniform_next;

    for(size_t i = 0; i < items_num; i++) {
	item = rand_next();

	if(random_get_put(type)) {
	    fprintf(fp, "UPDATE %lu\n", item);
	} else {
	    size_t scan_length = uniform_next() % 99 + 1;
	    fprintf(fp, "SCAN %lu %lu\n", item, scan_length);
	}
    }

    fclose(fp);
}

void create_workload_f(int type, int zipfian, size_t items_num) {
    if(type < 5) {
	printf("wrong wkld type %d\n", type);
	exit(1);
    }

    size_t item;
    FILE *fp;

    items_num = items_num / 4; // only for figure 8

    char *dist_name = malloc(sizeof(char) * 10);
    if(zipfian) 
	dist_name = "_zipf.trace";
    else 
	dist_name = "_unif.trace";

    char *type_name = malloc(sizeof(char) * 10);
    switch(type) {
	case WORKLOAD_F:
	    type_name = "f";
    }

    char *file_name = malloc(sizeof(char) * 100);
    strcpy(file_name, "txns");
    strcat(file_name, type_name);
    strcat(file_name, dist_name);

    remove(file_name);
    fp = fopen(file_name, "a+");

    for(size_t i = 0; i < items_num; i++) {
	if(zipfian)
	    item = zipf_next();
	else
	    item = uniform_next();

	fprintf(fp, "UPDATE %lu\n", item);
    }

    fclose(fp);
}


int main(int argc, char **argv) {
    if (argc < 4) {
	printf("Usage:\n");
	printf("1. workload type: a, b, c, d, e\n");
	printf("2. key distribution: zipf, unif\n");
	printf("3. number of items\n");
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
    } else if (strcmp(argv[1], "d") == 0) {
	wl = WORKLOAD_D;
    } else if (strcmp(argv[1], "e") == 0) {
	wl = WORKLOAD_E;
    } else if (strcmp(argv[1], "f") == 0) {
	wl = WORKLOAD_F;
    } else {
	fprintf(stderr, "Unknown workload: %s\n", argv[1]);
	exit(1);
    }

    // Then read key type
    int zipf;
    if (strcmp(argv[2], "unif") == 0) {
	zipf = MONO_KEY;
    } else if (strcmp(argv[2], "zipf") == 0) {
	zipf = RAND_KEY;
    } else {
	fprintf(stderr, "Unknown key type: %s\n", argv[2]);
	exit(1);
    }

    size_t items_num = atol(argv[3]);

    init_zipf_generator(1, items_num);
    init_latestgen(items_num);

    create_workload_load(wl, items_num);

    if(wl < 3)
	create_workload_abc(wl, zipf, items_num);
    else if(wl == 3)
	create_workload_d(wl, zipf, items_num);
    else if(wl == 4)
	create_workload_e(wl, zipf, items_num);
    else 
	create_workload_f(wl, zipf, items_num);

    return 0;
}
