#!/bin/bash

export I_MPI_PIN_RESPECT_CPUSET=0
export I_MPI_PIN_RESPECT_HCA=0
export I_MPI_PIN_DOMAIN=node
export I_MPI_PIN_PROCESSOR_LIST=allcores:map=scatter
export OSPRAY_SET_AFFINITY=0
# TODO: PBS support for theta
export OSPRAY_JOB_NAME=${SLURM_JOB_NAME}-${SLURM_JOBID}
export OMP_NUM_THREADS=$OSPRAY_THREADS

echo "OSPRAY_THREADS=$OSPRAY_THREADS"
BENCH_ARGS="-compositor $BENCH_COMPOSITOR -n $BENCH_ITERS -img $IMAGE_SIZE_X $IMAGE_SIZE_Y"
if [ -n "$TACC" ]; then
	ibrun ./benchmark $BENCH_ARGS
elif [ "$MACHINE" == "wopr" ]; then
	mpirun -np $SLURM_JOB_NUM_NODES -ppn 1 ./benchmark $BENCH_ARGS
elif [ "$MACHINE" == "theta" ]; then
	aprun -n $THETA_USE_NODES -N 1 -d 64 -cc depth ./benchmark $BENCH_ARGS
fi

