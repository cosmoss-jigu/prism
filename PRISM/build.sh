#!/bin/bash -e

DIR="$( cd "$( dirname "$0" )" && pwd -P )"
echo $DIR

rm -rf $DIR/build
mkdir -p $DIR/build

cd $DIR/build

cmake ..; make -j $(nproc);

if [ $? -ne 0 ]; then
    echo "========================================"
    echo "Fail(make PRISM) ... $?"
    echo "========================================"
    exit 1
fi

make install
