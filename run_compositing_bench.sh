#!/bin/bash

export I_MPI_PIN_RESPECT_CPUSET=0
export I_MPI_PIN_RESPECT_HCA=0
export I_MPI_PIN_DOMAIN=node
#export I_MPI_PIN_PROCESSOR_LIST=allcores:map=scatter
#export I_MPI_PIN_PROCESSOR_LIST=allcores:map=bunch
export OSPRAY_SET_AFFINITY=0
# TODO: Cobalt support for theta
export OMP_NUM_THREADS=$OSPRAY_THREADS

#source /opt/intel/itac_2018/bin/itacvars.sh
#export LD_PRELOAD=$LD_PRELOAD:$VT_SLIB_DIR/libVT.so
#export VT_LOGFILE_PREFIX=${SLURM_JOB_NAME}-${SLURM_JOBID}
#mkdir $VT_LOGFILE_PREFIX
#TRACE_ARG="-trace"

if [ -n "$WORK_DIR" ]; then
	echo "Changing to $WORK_DIR"
	cd $WORK_DIR
fi


if [ -n "$SLURM_JOB_NAME" ]; then
	export OSPRAY_JOB_NAME=${SLURM_JOB_NAME}-${SLURM_JOBID}
elif [ -n "$THETA_JOBNAME" ]; then
	export OSPRAY_JOB_NAME=${THETA_JOBNAME}-${COBALT_JOBID}
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
	# On Theta we run jobs for 2-128 nodes on the same 128 node allocation
	# since 128 is the min size.
	if [ "$COBALT_PARTSIZE" == "128" ]; then
		node_counts=(2 4 8 16 32 64 128)
		for i in "${node_counts[@]}"; do
			subjob_name="bench_${BENCH_COMPOSITOR}_${i}n_${IMAGE_SIZE_X}x${IMAGE_SIZE_Y}"
			logfile=${OSPRAY_JOB_NAME}.txt

			export OSPRAY_JOB_NAME=${subjob_name}-${COBALT_JOBID}

			export BENCH_ARGS="-compositor $BENCH_COMPOSITOR \
				-n $BENCH_ITERS \
				-img $IMAGE_SIZE_X $IMAGE_SIZE_Y \
				-o $OSPRAY_JOB_NAME"

			echo "Running $subjob_name"
			printenv > $logfile
			aprun -n $i -N 1 -d 64 -cc depth $BUILD_DIR/benchmark $BENCH_ARGS >> $logfile 2>&1
		done
	else
		aprun -n $COBALT_PARTSIZE -N 1 -d 64 -cc depth $BUILD_DIR/benchmark $BENCH_ARGS
	fi
fi

