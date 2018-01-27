#!/bin/bash

echo "OSPRAY_THREADS=$OSPRAY_THREADS"
BENCH_ARGS="-compositor $BENCH_COMPOSITOR -n $BENCH_ITERS -img $IMAGE_SIZE_X $IMAGE_SIZE_Y"
if [ -n "$TACC" ]; then
	ibrun ./benchmark $BENCH_ARGS
elif [ "$MACHINE" == "wopr" ]; then
	mpirun -np $SLURM_JOB_NUM_NODES -ppn 1 ./benchmark $BENCH_ARGS
elif [ "$MACHINE" == "theta" ]; then
	aprun -n $THETA_USE_NODES -N 1 -d 64 -cc depth ./benchmark $BENCH_ARGS
fi

