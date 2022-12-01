#!/bin/bash -e

rm -rf build
mkdir -p build

DIR="$( cd "$( dirname "$0" )" && pwd -P )"
echo $DIR
cd $DIR/build
cmake ..; make -j $(nproc);

if [ $? -ne 0 ]; then
    echo "========================================"
    echo "Fail... $?"
    echo "========================================"
    exit 1
fi

cd $DIR
#ln -s $DIR/build/tests/KeyIndex/KeyIndexTest KeyIndexTest
#ln -s $DIR/build/tests/ValueStorage/ValueStorageTest ValueStorageTest
