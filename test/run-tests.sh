#!/bin/bash
killall westeros-unittest
pushd brcm/external/install/bin
export LD_LIBRARY_PATH=../lib
export LD_PRELOAD=../lib/libwesteros-ut-em.so
export XDG_RUNTIME_DIR=/tmp
if [ "$1" = "valgrind" ]
then
  valgrind --leak-check=yes ./westeros-unittest -w
else
  ./westeros-unittest $@
fi
result=$?
popd
unset LD_PRELOAD
unset LD_LIBRARY_PATH
./get-coverage.sh
echo $result

