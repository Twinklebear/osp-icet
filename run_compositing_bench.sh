#!/bin/bash

echo "OSPRAY_THREADS= $OSPRAY_THREADS"
if [ -n "$TACC" ]; then
	ibrun ./benchmark -compositor $BENCH_COMPOSITOR -n $BENCH_ITERS \
		-img $IMAGE_SIZE_X $IMAGE_SIZE_Y
elif [ "$MACHINE" == "wopr" ]; then
	mpirun -np $SLURM_JOB_NUM_NODES -ppn 1 \
		./benchmark -compositor $BENCH_COMPOSITOR -n $BENCH_ITERS \
		-img $IMAGE_SIZE_X $IMAGE_SIZE_Y
fi

