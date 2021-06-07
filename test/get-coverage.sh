#!/bin/bash
case $1 in
  brcm)
  pushd brcm
  gcov libwesteros_compositor_la-westeros-compositor.cpp -o .libs/ > /dev/null 2>&1
  gcov westeros-render-gl.cpp -o .libs/ > /dev/null 2>&1
  gcov westeros-render-embedded.cpp -o .libs/ > /dev/null 2>&1
  cd essos
  gcov libessos_la-essos.cpp -o .libs/ > /dev/null 2>&1
  gcov libessosrmgr_la-essos-resmgr.cpp -o .libs/ > /dev/null 2>&1
  cd ..
  cd brcm/westeros-sink
  gcov libgstwesterossink_la-westeros-sink.c -o .libs/ > /dev/null 2>&1
  gcov libgstwesterossink_la-westeros-sink-soc.c -o .libs/ > /dev/null 2>&1
  cd ../..
  cd brcm/westeros-render-nexus
  gcov westeros-render-nexus.cpp -o .libs/ > /dev/null 2>&1
  cd ../..
  cd brcm
  gcov westeros-gl/libwesteros_gl_la-westeros-gl.cpp -o westeros-gl/.libs/ > /dev/null 2>&1
  cd ..
  ../parse-coverage westeros-compositor.cpp.gcov westeros-render-embedded.cpp.gcov westeros-render-gl.cpp.gcov essos/essos.cpp.gcov essos/essos-resmgr.cpp.gcov essos/essos-resmgr-uds.cpp.gcov brcm/westeros-render-nexus/westeros-render-nexus.cpp.gcov brcm/westeros-gl.cpp.gcov brcm/westeros-sink/westeros-sink.c.gcov brcm/westeros-sink/westeros-sink-soc.c.gcov
  ;;
  drm)
  pushd drm
  gcov libwesteros_compositor_la-westeros-compositor.cpp -o .libs/ > /dev/null 2>&1
  gcov westeros-render-gl.cpp -o .libs/ > /dev/null 2>&1
  gcov westeros-render-embedded.cpp -o .libs/ > /dev/null 2>&1
  cd essos
  gcov libessos_la-essos.cpp -o .libs/ > /dev/null 2>&1
  gcov libessosrmgr_la-essos-resmgr.cpp -o .libs/ > /dev/null 2>&1
  cd ..
  cd v4l2/westeros-sink
  gcov libgstwesterossink_la-westeros-sink.c -o .libs/ > /dev/null 2>&1
  gcov libgstwesterossink_la-westeros-sink-soc.c -o .libs/ > /dev/null 2>&1
  cd ../..
  cd drm
  gcov westeros-gl/libwesteros_gl_la-westeros-gl.cpp -o westeros-gl/.libs/ > /dev/null 2>&1
  cd ..
  ../parse-coverage westeros-compositor.cpp.gcov westeros-render-embedded.cpp.gcov westeros-render-gl.cpp.gcov essos/essos.cpp.gcov essos/essos-resmgr.cpp.gcov essos/essos-resmgr-uds.cpp.gcov drm/westeros-gl.c.gcov v4l2/westeros-sink/westeros-sink.c.gcov v4l2/westeros-sink/westeros-sink-soc.c.gcov
  ;;
  *)
  echo "bad platform"
  exit ;;
esac
popd

