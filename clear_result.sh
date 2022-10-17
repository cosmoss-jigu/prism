#!/bin/bash

RESULT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )/result"
rm -f $RESULT_DIR/*.tmp 
rm -f $RESULT_DIR/*.dat
