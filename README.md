# Optimizing Key-Value Store for Modern Heterogeneous Storage Devices with *PRISM*
This repository contains the source code for *PRISM*.


## Dependencies
- PMDK: https://github.com/pmem/pmdk
- liburing: https://github.com/axboe/liburing
- Install other packages
```
sudo dnf -y update
sudo dnf -y install ndctl ipmctl
dnf search pmem
sudo dnf -y install libpmem-devel librpmem-devel libpmemblk-devel libpmemlog-devel libpmemobj-devel libpmemobj++-devel libpmempool-devel
sudo dnf -y install zlib-devel libatomic autoconf numactl-devel jemalloc-devel gtest-devel tbb-devel boost-devel gperftools
```

## System Confiugrations
The artifact was tested in a machine with following specifications:
- Fedora 29 or higher
- Linux kernel v5.14.18.
- two 20-core Intel(R) Xeon(R) Gold 5218R CPU @ 2.10GHz
- 6 x 16 GB DRAM per socket
- 6 x 128 GB NVM per socket
- 8 x 1 TB Samsung 980 PRO SSDs (with two NVMe RAID Controller HighPoint SSD7103)
- x86-64 CPU supporting AVX512 instructions
```sh
#You can check using the following command:
$cat /proc/cpuinfo | egrep -ho 'avx[^ ]*' | sort -u
```

## Compiling and Reproducing Results

### Mount 12 NVMs and 8 SSDs
If your hardware device environment is different from the above environment,  
you need to modify the file *devices_mount.sh*.
```sh
$bash prepare_devices.sh
```

### Compile *PRISM*
```sh
bash ./PRISM/build.sh
make -j 99;
```

### Generating YCSB workloads
1. Configure Workload Parameters
    - Distribution type: Zipfian, Uniform
    - Workload type: A, B, C, D, E
    - Workload dir
    - Number of Key-value items
    - Zipfian coefficient
```sh
vim ./ycsb_generator/generator.sh
```
2. Generate YCSB Workloads
```sh
bash ./ycsb_generator/generator.sh
```
The generated workload files will be in "Workload dir"


### Launch Benchmarks
```sh
./artifact_evaluation/run_bench.sh
```
To learn more about *PRISM* configuration, please refer to *PRISM/include/mts-config.h*


## Notes
- If your system resources (e.g., CPU, NVMs, and SSDs) are different from the submitted paper,  
the experimental results may also be different.
- The detailed source code of the PRISM components is in directory *PRISM/src/*.
    - `MTS.cpp`: *PRISM*'s operations
    - `KeyIndex.h`: Persistent Key Index (PKI) on NVM
    - `AddressTable.*` : Heterogeneous Storage Indirection Table (HSIT) on NVM
    - `OpLog.*`: Persistent Write Buffer (PWB) on NVM
    - `ValueStorage.*`: Value Storage on Flash SSD
    - `CacheThread.*`: Scan-aware Value Cache (SVC) on DRAM
    - `AIO.*`: Opportunistic Thread Combining for Optimized Read


## Authors
- Yongju Song (Sungkyunkwan University) <yongju@skku.edu>
- Wook-Hee Kim (Konkuk University) <wookhee@konkuk.ac.kr>
- Sumit Kumar Monga (Virginia Tech) <sumitkm@vt.edu>
- Changwoo Min (Virginia Tech) <changwoo@vt.edu>
- Young Ik Eom (Sungkyunkwan University) <yieom@skku.edu>
