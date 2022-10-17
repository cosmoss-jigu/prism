#!/bin/bash -e

sudo make clean; ./PRISM/build.sh; make -j 99;
