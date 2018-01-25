#!/bin/bash

set -x
echo $OSPRAY_THREADS
ibrun ./benchmark -compositor $BENCH_COMPOSITOR -n $BENCH_ITERS \
  -img $IMAGE_SIZE_X $IMAGE_SIZE_Y

