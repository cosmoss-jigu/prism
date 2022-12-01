#!/usr/bin/env bash

#python3 cpu-topology.py > ../include/numa-config.h
#python3 cpu-topology.py > ./hydralist/include/numa-config.h
#python3 cpu-topology.py > ./hydralist_string/include/numa-config.h

DIR="$( cd "$( dirname "$0" )" && pwd -P )"
echo "### Get NUMA configuration ================================================"
echo $DIR

python3 $DIR/tools/cpu-topology.py > $DIR/numa-config.h
python3 $DIR/tools/cpu-topology.py > $DIR/PRISM/include/numa-config.h
python3 $DIR/tools/cpu-topology.py > $DIR/PRISM/lib/pactree/include/numa-config.h
