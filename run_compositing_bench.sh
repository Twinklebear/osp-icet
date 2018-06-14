#!/bin/bash

export I_MPI_PIN_RESPECT_CPUSET=0
export I_MPI_PIN_RESPECT_HCA=0
export I_MPI_PIN_DOMAIN=node
#export I_MPI_PIN_PROCESSOR_LIST=allcores:map=scatter
#export I_MPI_PIN_PROCESSOR_LIST=allcores:map=bunch
export OSPRAY_SET_AFFINITY=0
# TODO: PBS support for theta
export OMP_NUM_THREADS=$OSPRAY_THREADS

#source /opt/intel/itac_2018/bin/itacvars.sh
#export LD_PRELOAD=$LD_PRELOAD:$VT_SLIB_DIR/libVT.so
#export VT_LOGFILE_PREFIX=${SLURM_JOB_NAME}-${SLURM_JOBID}
#mkdir $VT_LOGFILE_PREFIX
#TRACE_ARG="-trace"

echo "TRACING: ${OSPRAY_DP_API_TRACING}"

if [ -n "$WORK_DIR" ]; then
	echo "Changing to $WORK_DIR"
	cd $WORK_DIR
fi


if [ -n "$SLURM_JOB_NAME" ]; then
	export OSPRAY_JOB_NAME=${SLURM_JOB_NAME}-${SLURM_JOBID}
elif [ -n "$PBS_JOBNAME" ]; then
	export OSPRAY_JOB_NAME=${PBS_JOBNAME}-${PBS_JOBID}
fi

echo "OSPRAY_THREADS=$OSPRAY_THREADS"
export BENCH_ARGS="-compositor $BENCH_COMPOSITOR \
	-n $BENCH_ITERS \
	-img $IMAGE_SIZE_X $IMAGE_SIZE_Y \
	-o $OSPRAY_JOB_NAME"

printenv

if [ -n "$TACC" ]; then
	module restore
	ibrun $TRACE_ARG $BUILD_DIR/benchmark $BENCH_ARGS
elif [ "$MACHINE" == "wopr" ]; then
	mpirun -np $SLURM_JOB_NUM_NODES -ppn 1 $BUILD_DIR/benchmark $BENCH_ARGS
elif [ "$MACHINE" == "theta" ]; then
	aprun -n $THETA_USE_NODES -N 1 -d 64 -cc depth $BUILD_DIR/benchmark $BENCH_ARGS
fi

