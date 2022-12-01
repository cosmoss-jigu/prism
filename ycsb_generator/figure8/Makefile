CC=gcc
CFLAGS=-g -O2 -ggdb3 -lm #-DSLMDB #-lpthread -lstdc++ -Wall -

MAIN_OBJ=random.o ycsb_generator.o

main: $(MAIN_OBJ)
	$(CC) random.o ycsb_generator.o $(CFLAGS) -o ycsb_generator 

random.o: random.c random.h
	$(CC) $(CFLAGS) -c -o random.o random.c

ycsb_generator.o: main.c ycsb_generator.h
	$(CC) $(CFLAGS) -c -o ycsb_generator.o main.c

clean:
	rm -f ycsb_generator
	rm -f *.o
