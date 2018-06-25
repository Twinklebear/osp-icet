#!/bin/bash

if [ -n "$TACC" ]; then
	module restore
fi

export I_MPI_PIN_RESPECT_CPUSET=0
export I_MPI_PIN_RESPECT_HCA=0
export I_MPI_PIN_DOMAIN=omp
if [ "$JOB_QUEUE" == "skx-normal" ]; then
	export I_MPI_PIN_PROCESSOR_LIST=all
else
	export I_MPI_PIN_PROCESSOR_LIST=allcores
fi
export OSPRAY_SET_AFFINITY=0
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

# Only one of these will be non-empty
JOBID="${SLURM_JOBID}${COBALT_JOBID}"
NPROCS="${SLURM_NNODES}${COBALT_PARTSIZE}"

#compositors=(icet ospray)
compositors=(ospray)
for c in "${compositors[@]}"; do
	export OSPRAY_JOB_NAME="bench_${c}_${NPROCS}n_${IMAGE_SIZE_Y}x${IMAGE_SIZE_Y}-${JOBID}"
	if [ -n "$JOB_QUEUE" ]; then
		export OSPRAY_JOB_NAME="${OSPRAY_JOB_NAME}-$JOB_QUEUE"
	fi

	export BENCH_ARGS="-compositor $c \
		-n $BENCH_ITERS \
		-img $IMAGE_SIZE_X $IMAGE_SIZE_Y \
		-o $OSPRAY_JOB_NAME"
	logfile=${OSPRAY_JOB_NAME}.txt

	printenv > $logfile

	if [ -n "$TACC" ]; then
		ibrun $TRACE_ARG $BUILD_DIR/benchmark $BENCH_ARGS >> $logfile 2>&1
	elif [ "$MACHINE" == "wopr" ]; then
		mpirun -np $SLURM_JOB_NUM_NODES -ppn 1 $BUILD_DIR/benchmark $BENCH_ARGS >> $logfile 2>&1
	elif [ "$MACHINE" == "theta" ]; then
		# On Theta we run jobs for 2-128 nodes on the same 128 node allocation
		# since 128 is the min size.
		if [ "$COBALT_PARTSIZE" == "128" ]; then
			node_counts=(2 4 8 16 32 64 128)
			for i in "${node_counts[@]}"; do
				export OSPRAY_JOB_NAME="bench_${c}_${i}n_${IMAGE_SIZE_Y}x${IMAGE_SIZE_Y}-${JOBID}"
				logfile=${OSPRAY_JOB_NAME}.txt

				export BENCH_ARGS="-compositor $c \
					-n $BENCH_ITERS \
					-img $IMAGE_SIZE_X $IMAGE_SIZE_Y \
					-o $OSPRAY_JOB_NAME"

				echo "Running $subjob_name"
				printenv > $logfile
				aprun -n $i -N 1 -d 64 -cc depth $BUILD_DIR/benchmark $BENCH_ARGS >> $logfile 2>&1
			done
		else
			logfile=${OSPRAY_JOB_NAME}.txt
			aprun -n $COBALT_PARTSIZE -N 1 -d 64 -cc depth $BUILD_DIR/benchmark $BENCH_ARGS >> $logfile 2>&1
		fi
	fi
done

