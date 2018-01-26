#!/bin/bash

echo "OSPRAY_THREADS= $OSPRAY_THREADS"
set -x
ibrun ./benchmark -compositor $BENCH_COMPOSITOR -n $BENCH_ITERS \
  -img $IMAGE_SIZE_X $IMAGE_SIZE_Y

