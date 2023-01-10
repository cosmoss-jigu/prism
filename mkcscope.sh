#!/bin/bash

rm -rf cscope.files cscope.out
rm -rf tags

find . \( -name '*.c' -o -name '*.cpp' -o -name '*.cc' -o -name '*.h' \) -print > cscope.files

ctags -R #for C language
#ctags -R --c++-kinds=+p --fields=+iaS --extra=+q --language-force=C++ #for C++ language

cscope -i cscope.files
