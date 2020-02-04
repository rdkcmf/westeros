#!/bin/bash
unset LD_PRELOAD
case $1 in
  brcm)
  pushd brcm/external/install/bin ;;
  drm)
  export LD_PRELOAD=../lib/libwesteros_gl.so.0.0.0
  pushd drm/external/install/bin ;;
  *)
  echo "bad platform"
  exit ;;
esac
args=""
argnum=0
for ARG in $@; do
  if [ "$argnum" != "0" ]
  then
     args="$args $ARG"
  fi
  argnum=$argnum+1
done
killall westeros-unittest
export LD_LIBRARY_PATH=../lib
export LD_PRELOAD=$LD_PRELOAD:../lib/libwesteros-ut-em.so
export XDG_RUNTIME_DIR=/tmp
export GST_REGISTRY=.
export GST_PLUGIN_SYSTEM_PATH=../lib/gstreamer-1.0
if [ "$2" = "valgrind" ]
then
  valgrind --leak-check=yes ./westeros-unittest -w
elif [ "$2" = "gdb" ]
then
  gdb ./westeros-unittest
else
  ./westeros-unittest $args
fi
result=$?
popd
unset LD_PRELOAD
unset LD_LIBRARY_PATH
./get-coverage.sh $1
echo $result

