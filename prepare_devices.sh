#!/bin/sh

#name=${1:?"requires an argument"}

#umount /mnt/pmem*
#ndctl destroy-namespace -f all
#ipmctl delete -goal
#ipmctl create -goal persistentmemorytype=appdirect
#ipmctl create -goal persistentmemorytype=appdirectnotinterleaved

if [ $EUID -ne 0 ]; then
    echo "This script must be run as root" 
    exit 1
fi

#Disable hyperthreading
for i in $(seq 40 80); do
    echo 0 > /sys/devices/system/cpu/cpu$i/online
done

#Disable frequency scaling to limit stddev
echo 1 > /sys/devices/system/cpu/intel_pstate/no_turbo
echo 0 > /proc/sys/kernel/numa_balancing
systemctl disable ondemand
for i in $(seq 0 80); do
    echo "performance" > /sys/devices/system/cpu/cpu$i/cpufreq/scaling_governor
done

#pmem
for SET in $(seq 0 1);
do
    umount /mnt/pmem${SET}
    rm -r /mnt/pmem${SET}

    sudo ndctl create-namespace --mode=fsdax
    sleep 1
    mkdir -p /mnt/pmem${SET}

    yes | mkfs.ext4 -E lazy_itable_init=0,lazy_journal_init=0 "/dev/pmem${SET}"
    tune2fs -O ^has_journal "/dev/pmem${SET}"
    e2fsck -f "/dev/pmem${SET}"

    mount -o rw,noatime,nodiratime,block_validity,delalloc,nojournal_checksum,barrier,user_xattr,acl,dax -t ext4 "/dev/pmem${SET}" "/mnt/pmem${SET}"

    mkdir -p /mnt/pmem${SET}/prism

    sleep 1
done

ndctl list -u

for SET in $(seq 0 7);
do
    umount /mnt/hpt${SET}
    rm -r /mnt/hpt${SET}
done

modprobe -r nvme 
modprobe nvme poll_queues=0
sleep 1

#highpoint raid ext4
for SET in $(seq 0 7);
do
    NVME=$SET
    if [ $SET -gt 3 ]; then
	NVME=$(($SET+1))
    fi

    mkdir -p /mnt/hpt${SET}
    yes | mkfs.xfs -f "/dev/nvme${NVME}n1"
    mount -o noatime,nodiratime "/dev/nvme${NVME}n1" "/mnt/hpt${SET}"

    mkdir -p /mnt/hpt${SET}/prism
done

chmod -R 777 "/mnt"
