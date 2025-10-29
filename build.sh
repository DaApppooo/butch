#!/bin/env bash

SRC="parse.cpp read.cpp butch.cpp list.cpp"
CMD="./test.butch ./test.code"

if [[ $1 = "pg" ]];
then
  # to use with KCacheGrind
  g++ readparse.cpp $SRC -o readparse.out -g || exit 1
  ./readparse ./test.butch ./test.code
  valgrind --tool=callgrind --callgrind-out-file=callgrind.output ./readparse test.butch test.code || exit 1
elif [[ $1 = "san" ]];
then
  g++ readparse.cpp $SRC -o readparse.out -g --sanitize=address || exit 1
  ./readparse ./test.butch ./test.code
elif [[ $1 = "db" ]];
then
  # simple gdb
  g++ readparse.cpp $SRC -o readparse.out -g || exit 1
  gdb --args $CMD || exit 1
  perf report -g graph
elif [[ $1 == "clean" ]];
then
  rm ./flamegraph.svg
  rm ./out.perf
  rm ./out.folded
  rm ./callgrind.output
elif [[ $1 == "shared" ]];
then
  g++ $SRC -fPIC -shared -O3 -o libbutch.so || exit 1
  \ls libbutch.so
else
  g++ readparse.cpp $SRC -o readparse -g || exit 1
  $CMD || exit 1
fi
