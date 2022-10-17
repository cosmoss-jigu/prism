#!/bin/bash -e

./clear_prism.sh

rm -rf build
mkdir -p build

DIR="$( cd "$( dirname "$0" )" && pwd -P )"
echo $DIR

cd $DIR/build
cmake ..; make -j 99;

if [ $? -ne 0 ]; then
    echo "========================================"
    echo "Fail(make PRISM) ... $?"
    echo "========================================"
    exit 1
fi

make install

cd $DIR
ln -sf $DIR/build/tests/PRISM/PRISMTest PRISMTest
