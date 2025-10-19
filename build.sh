#!/bin/env bash

SRC="readparse.cpp parse.cpp read.cpp butch.cpp list.cpp"
CMD="./readparse ./test.butch ./test.code"

if [[ $1 = "pg" ]];
then
  # to use with KCacheGrind
  g++ $SRC -o readparse.out -g || exit 1
  ./readparse ./test.butch ./test.code
  valgrind --tool=callgrind --callgrind-out-file=callgrind.output ./readparse test.butch test.code || exit 1
elif [[ $1 = "san" ]];
then
  g++ $SRC -o readparse.out -g --sanitize=address || exit 1
  ./readparse ./test.butch ./test.code
elif [[ $1 = "db" ]];
then
  # simple gdb
  g++ $SRC -o readparse.out -g || exit 1
  gdb --args $CMD || exit 1
  perf report -g graph
elif [[ $1 == "clean" ]];
then
  rm ./flamegraph.svg
  rm ./out.perf
  rm ./out.folded
  rm ./callgrind.output
else
  g++ $SRC -o readparse -g || exit 1
  $CMD || exit 1
fi
