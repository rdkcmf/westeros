#!/bin/bash
unset LD_PRELOAD
export LD_LIBRARY_PATH=../lib
case $1 in
  brcm)
  pushd brcm/external/install/bin ;;
  drm)
  pushd drm/external/install/bin
  export LD_PRELOAD=../lib/libwesteros_gl.so.0.0.0 ;;
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
export LD_PRELOAD=$LD_PRELOAD:../lib/libwesteros-ut-em.so
export XDG_RUNTIME_DIR=/tmp
unset WAYLAND_DISPLAY
export GST_REGISTRY=.
export GST_PLUGIN_SYSTEM_PATH=../lib/gstreamer-1.0
#export WESTEROS_SINK_USE_ESSRMGR=1
if [ "$2" = "valgrind" ]
then
  valgrind --leak-check=yes --track-origins=yes ./westeros-unittest -w $3
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

