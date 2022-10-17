#!/bin/bash -e

DIST_TYPE=("zipf") #zipf or unif
WORKLOAD_TYPE=(a b c d e f) #type a b c d e ## NOTE: type f for measuring SSD-level WAF
ITEM_NUM=8000000
ZIPF=("0.5" "0.9" "0.99" "1.2" "1.5")

rm -f *.trace

for zipf in "${ZIPF[@]}"
do
    sed -i "69s/.*/zipfianconstant = ${zipf};/g" random.c
    make clean; make;

    for dist_type in "${DIST_TYPE[@]}"
    do
	for workload_type in "${WORKLOAD_TYPE[@]}"
	do
	    item_num=$ITEM_NUM
	    eval ./ycsb_generator ${workload_type} ${dist_type} ${item_num};

	    echo "WORKLOAD_TYPE: $workload_type $dist_type ITEM_NUM: $item_num"
	
	done
    done

    mkdir -p ${zipf};
    mv *.trace ${zipf}/;
done
