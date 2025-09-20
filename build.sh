#!/bin/env bash

SRC="readtest.cpp parse.cpp read.cpp butch.cpp list.cpp"
CMD="./readtest ./test.butch ./test.code"

if [[ $1 = "pg" ]];
then
  # to use with KCacheGrind
  g++ $SRC -o readtest -g || exit 1
  ./readtest ./test.butch ./test.code
  valgrind --tool=callgrind --callgrind-out-file=callgrind.output ./readtest test.butch test.code || exit 1
elif [[ $1 = "san" ]];
then
  g++ $SRC -o readtest -g --sanitize=address || exit 1
  ./readtest ./test.butch ./test.code
elif [[ $1 = "db" ]];
then
  # simple gdb
  g++ $SRC -o readtest -g || exit 1
  gdb --args $CMD || exit 1
elif [[ $1 = "fg" ]];
then
  # flame graph
  g++ $SRC -o readtest -pg || exit 1
  sudo perf record -g -- $CMD || exit 1
  sudo chown table:table out.perf
  sudo chown table:table perf.data
  # perf script > out.perf || exit 1
  # stackcollapse-perf.pl out.perf > out.folded || exit 1
  # flamegraph.pl out.folded > flamegraph.svg || exit 1
  # librewolf ./flamegraph.svg || exit 1
  perf report -g graph
elif [[ $1 == "clean" ]];
then
  rm ./flamegraph.svg
  rm ./out.perf
  rm ./out.folded
  rm ./callgrind.output
else
  g++ $SRC -o readtest -g || exit 1
  $CMD || exit 1
fi
