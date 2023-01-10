#!/bin/bash -e

#are devices mounted?
if grep -qs '/mnt/pmem0 ' /proc/mounts; then
    echo "Devices are mounted."
else
    echo "Devices are NOT mounted."
    exit 1
fi


TIMESTAMP=`date +%y-%m-%d-%H:%M`
CURRENT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"
ROOT_DIR="${CURRENT_DIR}/.."
PRISM_DIR="${ROOT_DIR}/PRISM"			    #PRISM directory.
RESULT_DIR="${CURRENT_DIR}/result/figure10"		    #Results go this directory.
WORKLOAD_SRC="${ROOT_DIR}/ycsb_generator"	    #Generated YCSB workloads are in this directory.
WORKLOAD_DST="${ROOT_DIR}/workloads"		    
THROUGHPUT_OUTPUT="${CURRENT_DIR}/throughput.txt"   #Results(throughput) are written in this file.
INSERT_OUTPUT="${CURRENT_DIR}/insert.txt"	    #Results(insert throughput) are recorded in this file.

KEY_TYPE=(zipf)		    #"rand": skewed, "mono": uniformed.
WORKLOAD_TYPE=(c)   #Typing which workload_type will be tested.
CORE_NUM=(40)		    #how many CPU cores?    ##PRISM threads: CPU cores / 5 * 4 ##IO Completion threads: CPU cores * 5
ZIPF=("0.99")		    #Zipfian coefficient
WKLD_QD=(1 2 4 8 16 32 64)		    #the IO batch size in our asynchronous IO approach ref. Section 'Opportunistic Thread Combining'
WKLD_E_QD=(100)	    

SVC_SIZE=(20)	    #Size of Scan-aware Value Cache
PWB_SIZE=(16)	    #Size of Persistent Write Buffer
DISK_NUM=(8)	    #The number of SSDs

KV_SIZE=(1024)	    #512 2048 4096
PERF_MODE=(1 0)	    
#PERF_MODE(0): Measure various performance metrics, and prints them such as thp, SSD WAF, Avg. IO Batching Size, Read-hit count and Latency
#PERF_MODE(1): Only prints throughput. 
RECOVERY=(0)


#get numa configuration
bash ${ROOT_DIR}/get-numa-config.sh

#cleanup garbage files (obsolete files regarding PRISM)
CLEANUP()
{
    echo "### CLEARING EXISTING PRISM COMPONENTS ====================================="

    bash ${PRISM_DIR}/clear_prism.sh
}

#build PRISM and Benchmark tool for it
BUILD()
{
    cd ${ROOT_DIR}
    make clean;
    bash ${PRISM_DIR}/build.sh;
    make -j $(nproc);
    if [ $? -ne 0 ]; then
	echo "======================================="
	echo "Fail(make MTS) ... $?"
	echo "======================================="

	exit 1
    fi
}

#copy workload traces from 'ycsb_generator'
COPY_WORKLOAD_TRACE()
{
    workload_file="txns$1_$2.trace"
    zipf=$3

    if [[ ! -f $WORKLOAD_SRC/${zipf}/${workload_file} ]]
    then
	echo "Workload file($workload_file) does not exist."
	echo "Plase generate workload files (ref. ycsb_generator)"
	exit 1
    fi

    cp $WORKLOAD_SRC/$zipf/$workload_file $WORKLOAD_DST/
}

COPY_LOAD_TRACE()
{
    load_file="load.trace"

    if [[ ! -f $WORKLOAD_SRC/${ZIPF}/${load_file} ]]
    then
	echo "Load workload file does not exist."
	echo "Plase generate load workload files (ref. ycsb_generator.sh)"
	exit 1
    fi
    cp $WORKLOAD_SRC/$ZIPF/${load_file} $WORKLOAD_DST/
}

RUN_TEST()
{
    workload_type=$1
    key_type=$2
    thread_num=$3
    zipf=$4
    perf=$5
    run=0

    cmd="${ROOT_DIR}/workload ${workload_type} ${key_type} ${thread_num}"
    #cmd="./workload ${workload_type} ${key_type} ${thread_num} --insert-only"
    #cmd="./workload ${workload_type} ${key_type} ${thread_num} --recovery"

    workload="${workload_type}_${key_type}_${thread_num}_${zipf}_${perf}"
    output="${RESULT_DIR}/${workload}"

    if [ -e "${output}_${run}.out" ]; then
	while [[ -e "${output}_${run}.out" ]] ; do
	    run=$(( $run + 1 ))
	done
    fi

    output="${output}_${run}.out"

    #if [ -e "$output" ]; then
    #echo "skipping, existing $workload"
    #return;
    #fi

    $cmd 2>&1 | tee ${workload}.tmp
    if [ $? -ne 0 ]; then
	echo "======================================="
	echo exiting; status=$?
	echo "======================================="
	exit 1
    fi  

    mv ${workload}.tmp ${output}

    echo "${workload}_$(cat ${output} | grep "YCSB_INSERT")" >> ${INSERT_OUTPUT}
    echo "${workload}_$(cat ${output} | grep "YCSB_[ABCDE]")" >> ${THROUGHPUT_OUTPUT}

    echo "### Finished running benchmark ============================================="
    echo "Output: ${output}"
}


RUN_RECOVERY_TEST()
{
    workload_type=$1
    key_type=$2
    thread_num=$3
    zipf=$4
    perf=$5
    run=0

    cmd="./workload ${workload_type} ${key_type} ${thread_num} --recovery"

    workload="${workload_type}_${key_type}_${thread_num}_${zipf}_${perf}"
    output="${RESULT_DIR}/${workload}"

    if [ -e "${output}_${run}.out" ]; then
	while [[ -e "${output}_${run}.out" ]] ; do
	    run=$(( $run + 1 ))
	done
    fi

    output="${output}_${run}.out"

    #if [ -e "$output" ]; then
    #echo "skipping, existing $workload"
    #return;
    #fi

    $cmd 2>&1 | tee ${workload}.tmp
    if [ $? -ne 0 ]; then
	echo "======================================="
	echo exiting; status=$?
	echo "======================================="
	exit 1
    fi  

    mv ${workload}.tmp ${output}

    echo "${workload}_$(cat ${output} | grep "YCSB_INSERT")" >> ${INSERT_OUTPUT}
    echo "${workload}_$(cat ${output} | grep "YCSB_[ABCDE]")" >> ${THROUGHPUT_OUTPUT}

    echo "### Finished running benchmark ============================================="
    echo "Output: ${output}"
}


#################################################################################################################################


mkdir -p ${RESULT_DIR}
echo $TIMESTAMP >> $INSERT_OUTPUT
echo $TIMESTAMP >> $THROUGHPUT_OUTPUT


#copy workload traces from 'ycsb_generator'
COPY_LOAD_TRACE
for zipf in "${ZIPF[@]}"
do
    for key_type in "${KEY_TYPE[@]}"
    do
	for workload_type in "${WORKLOAD_TYPE[@]}"
	do
	    COPY_WORKLOAD_TRACE $workload_type $key_type $zipf
	done
    done
done

#run all the jobs
for perf_mode in "${PERF_MODE[@]}"
do
    if [[ $perf_mode == '1' ]];
    then
	sed -i "10s/.*/#add_definitions(-DMTS_STATS_LATENCY -DMTS_STATS_GET -DMTS_STATS_OPLOG_WAIT -DMTS_STATS_WAF)/g" ${PRISM_DIR}/CMakeLists.txt
    else 
	sed -i "10s/.*/add_definitions(-DMTS_STATS_LATENCY -DMTS_STATS_GET -DMTS_STATS_OPLOG_WAIT -DMTS_STATS_WAF)/g" ${PRISM_DIR}/CMakeLists.txt
    fi

    for zipf in "${ZIPF[@]}"
    do
	for wkld_qd in "${WKLD_QD[@]}"
	do
	    for kv_size in "${KV_SIZE[@]}"
	    do
		for disk_num in "${DISK_NUM[@]}"
		do
		    for pwb_size in "${PWB_SIZE[@]}"
		    do
			for svc_size in "${SVC_SIZE[@]}"
			do
			    for core_num in "${CORE_NUM[@]}"
			    do
				for recovery in "${RECOVERY[@]}"
				do
				    thread_num="`expr ${CORE_NUM} '*' 4 '/' 5`"
				    io_completer="`expr ${CORE_NUM} '/' 5`"

				    sed -i "5s/.*/#define MTS_THREAD_NUM ${thread_num}/g" ${PRISM_DIR}/include/mts-config.h
				    sed -i "6s/.*/#define KV_SIZE ${kv_size}UL/g" ${PRISM_DIR}/include/mts-config.h
				    sed -i "25s/.*/#define MTS_OPLOG_G_SIZE (${pwb_size}UL * 1024UL * 1024UL * 1024UL)/g" ${PRISM_DIR}/include/mts-config.h
				    sed -i "35s/.*/#define MTS_VS_DISK_NUM ${disk_num}/g" ${PRISM_DIR}/include/mts-config.h
				    sed -i "51s/.*/#define R_QD ${wkld_qd}/g" ${PRISM_DIR}/include/mts-config.h
				    sed -i "60s/.*/#define IO_COMPLETER_NUM ${io_completer}/g" ${PRISM_DIR}/include/mts-config.h
				    sed -i "65s/.*/#define MTS_DRAMCACHE_SIZE ((${svc_size}UL * 1024UL * 1024UL * 1024UL))/g" ${PRISM_DIR}/include/mts-config.h

				    BUILD

				    for key_type in "${KEY_TYPE[@]}"
				    do
					for workload_type in "${WORKLOAD_TYPE[@]}"
					do

					    if [[ $workload_type == "e" ]];
					    then
						sed -i "51s/.*/#define R_QD ${WKLD_E_QD}/g" ${PRISM_DIR}/include/mts-config.h
						BUILD
					    fi

					    CLEANUP

					    if [[ $recovery == '0' ]]; then
						echo "### EXEC ==================================================================="
						echo "WORKLOAD_TYPE: $workload_type KEY_TYPE: $key_type THREAD_NUM: $thread_num ZIPF: $zipf"
						echo "./workload ${workload_type} ${key_type} ${thread_num}"

						RUN_TEST $workload_type $key_type $thread_num $zipf $perf_mode

					    else
						echo "### EXEC RECOVERY TEST======================================================"
						echo "WORKLOAD_TYPE: $workload_type KEY_TYPE: $key_type THREAD_NUM: $thread_num ZIPF: $zipf"
						echo "./workload ${workload_type} ${key_type} ${thread_num} --recovery"

						RUN_RECOVERY_TEST $workload_type $key_type $thread_num $zipf $perf_mode
						exit 0
					    fi

					done
				    done
				done
			    done
			done
		    done
		done
	    done
	done
    done
done

