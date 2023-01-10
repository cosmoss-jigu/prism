# Prism: Optimizing Key-Value Store for Modern Heterogeneous Storage Devices
This repository contains the source code for ACM ASPLOS 2023.

*Yongju Song, Wook-Hee Kim, Sumit Kumar Monga, Changwoo Min, and Young Ik Eom,
"Prism: Optimizing Key-Value Store for Modern Heterogeneous Storage Devices,"
In Proceedings of the 28th ACM International Conference on Architectural Support for Programming Languages and Operating Systems, Volume 2 (ASPLOS ’23), March 25–29, 2023, Vancouver, BC, Canada.*


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
sudo dnf -y install liburing liburing-devel papi-devel automake
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
### Using Intel Optane Persistent Memory (optional)

```
sudo ipmctl create -goal PersistentMemoryType=AppDirect
sudo ndctl create-namespace -f -e namespace0.0 --mode=fsdax
sudo ndctl create-namespace -f -e namespace1.0 --mode=fsdax
sudo ndctl list --human
[
  {
    "dev":"namespace1.0",
    "mode":"fsdax",
    "map":"dev",
    "size":"744.19 GiB (799.06 GB)",
    "uuid":"cd39c10e-4fb2-4eb7-8dbf-61337a8a8049",
    "sector_size":512,
    "align":2097152,
    "blockdev":"pmem1"
  },
  {
    "dev":"namespace0.0",
    "mode":"fsdax",
    "map":"dev",
    "size":"744.19 GiB (799.06 GB)",
    "uuid":"acf03367-9787-4168-9f6c-e7831d31acdf",
    "sector_size":512,
    "align":2097152,
    "blockdev":"pmem0"
  }
]
```
For details, see [Intel Optane DC Persistent Memory Quick Start Guide](https://www.intel.com/content/dam/support/us/en/documents/memory-and-storage/data-center-persistent-mem/Intel_Optane_Persistent_Memory_Start_Up_Guide.pdf)

### Preparing 12 NVMs and 8 SSDs
If your hardware device environment is different from the above environment,  
you need to modify the file *devices_mount.sh*.
```sh
$bash prepare_devices.sh
$df -Th
/dev/pmem0                     ext4      733G   73M  696G   1% /mnt/pmem0
/dev/pmem1                     ext4      733G   73M  696G   1% /mnt/pmem1
/dev/nvme0n1                   xfs       932G  6.6G  925G   1% /mnt/hpt0
/dev/nvme1n1                   xfs       932G  6.6G  925G   1% /mnt/hpt1
/dev/nvme2n1                   xfs       932G  6.6G  925G   1% /mnt/hpt2
/dev/nvme3n1                   xfs       932G  6.6G  925G   1% /mnt/hpt3
/dev/nvme5n1                   xfs       932G  6.6G  925G   1% /mnt/hpt4
/dev/nvme6n1                   xfs       932G  6.6G  925G   1% /mnt/hpt5
/dev/nvme7n1                   xfs       932G  6.6G  925G   1% /mnt/hpt6
/dev/nvme8n1                   xfs       932G  6.6G  925G   1% /mnt/hpt7
```

### Compiling *Prism*
Compile the *Prism* source and its benchmark tools
```sh
./build.sh
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


### Running the Benchmarks
Basic Test
```
./workload a zipf 32
```
Test *Prism* with various configurations.
```sh
./run_bench.sh
```
To learn more about *Prism* configuration, please refer to *PRISM/include/mts-config.h*


## Notes
- If your system resources (e.g., CPU, NVMs, and SSDs) are different from the paper,  
the experimental results may also be different.
- The detailed source code of the *Prism* components is in directory *PRISM/src/*.
    - `MTS.cpp`: *Prism*'s operations
    - `KeyIndex.h`: Persistent Key Index on NVM
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
