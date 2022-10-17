CC = gcc 
CXX = g++ -std=c++17
MEMMGR = -lpapi -lpmem -lpmemobj -ljemalloc 
CFLAGS = -g -O3 -Wno-all -Wno-invalid-offsetof -mcx16 -DNDEBUG -DBWTREE_NODEBUG -include masstree/config.h -latomic -luring -ltcmalloc
SNAPPY = /usr/lib/libsnappy.so.1.3.0
all: workload
run_all: workload
	./workload a rand $(TYPE) $(THREAD_NUM) 
	./workload c rand $(TYPE) $(THREAD_NUM)
	./workload e rand $(TYPE) $(THREAD_NUM)
	./workload a mono $(TYPE) $(THREAD_NUM) 
	./workload c mono $(TYPE) $(THREAD_NUM)
	./workload e mono $(TYPE) $(THREAD_NUM)

libs: PRISM/libMTS.a masstree/mtIndexAPI.a

masstree/mtIndexAPI.a:
	(cd masstree && ./configure && make)

bwtree.o: ./BwTree/bwtree.h ./BwTree/bwtree.cpp libs
	        $(CXX) $(CFLAGS) -c -o bwtree.o ./BwTree/bwtree.cpp

workload.o: workload.cpp microbench.h index.h util.h ./papi_util.cpp ./PRISM/include/MTS.h ./BwTree/bwtree.h ./masstree/mtIndexAPI.hh
	$(CXX) $(CFLAGS) -I ./PRISM/include/ -I ./PRISM/src/ -c -o workload.o workload.cpp -I ./PRISM/lib/pactree/include/


workload: workload.o bwtree.o ./masstree/mtIndexAPI.a PRISM/libMTS.a
	$(CXX) $(CFLAGS) -o workload workload.o bwtree.o masstree/mtIndexAPI.a ./PRISM/libMTS.a ./PRISM/libtsoplog.a ./PRISM/libpactree.a ./PRISM/libpdlart.a $(MEMMGR) -lpthread -lm -ltbb -lnuma -latomic


clean:
	(cd PRISM && ./clear_prism.sh)
	(cd ycsb_generator && make clean)
	(cd ycsb_generator/figure8 && make clean)
	(cd masstree && make clean)
	$(RM) workload *.o *~ *.d
