#!/bin/bash

if [ -n "$TACC" ]; then
	module restore
fi

source ${SCRIPT_DIR}/set_ospray_vars.sh

if [ -n "$TACC_TRACING" ]; then
	source /opt/intel/itac_2018/bin/itacvars.sh
	export LD_PRELOAD=$LD_PRELOAD:$VT_SLIB_DIR/libVT.so
	export VT_LOGFILE_PREFIX=${SLURM_JOB_NAME}-${SLURM_JOBID}
	mkdir $VT_LOGFILE_PREFIX
	export TRACE_ARG="-trace"
fi

if [ -n "$WORK_DIR" ]; then
	echo "Changing to $WORK_DIR"
	cd $WORK_DIR
fi

# Only one of these will be non-empty
JOBID="${SLURM_JOBID}${COBALT_JOBID}"
NPROCS="${SLURM_NNODES}${COBALT_PARTSIZE}"

compositors=(ospray icet)
for c in "${compositors[@]}"; do
	export JOB_NAME="bench_${c}_${NPROCS}n_${IMAGE_SIZE_Y}x${IMAGE_SIZE_Y}-${JOBID}"
	if [ -n "$JOB_QUEUE" ]; then
		export JOB_NAME="${JOB_NAME}-$JOB_QUEUE"
	fi
	if [ -n "$PREFIX" ]; then
		export JOB_NAME="${PREFIX}-${JOB_NAME}"
	fi

	export OSPRAY_JOB_NAME="$SCRATCH/osp-icet/${JOB_NAME}"
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
			node_counts=(4 8 16 32 64 128)
			for i in "${node_counts[@]}"; do
				export JOB_NAME="bench_${c}_${i}n_${IMAGE_SIZE_Y}x${IMAGE_SIZE_Y}-${JOBID}"
				if [ -n "$JOB_QUEUE" ]; then
					export JOB_NAME="${JOB_NAME}-$JOB_QUEUE"
				fi
				if [ -n "$PREFIX" ]; then
					export JOB_NAME="${PREFIX}-${JOB_NAME}"
				fi
				export OSPRAY_JOB_NAME="$SCRATCH/osp-icet/${JOB_NAME}"
				logfile=${OSPRAY_JOB_NAME}.txt

				export BENCH_ARGS="-compositor $c \
					-n $BENCH_ITERS \
					-img $IMAGE_SIZE_X $IMAGE_SIZE_Y \
					-o $OSPRAY_JOB_NAME"

				echo "Running $JOB_NAME"
				printenv > $logfile
				aprun -n $i -N 1 -d 64 -cc depth $BUILD_DIR/benchmark $BENCH_ARGS >> $logfile 2>&1
			done
		else
			aprun -n $COBALT_PARTSIZE -N 1 -d 64 -cc depth $BUILD_DIR/benchmark $BENCH_ARGS >> $logfile 2>&1
		fi
	fi
done

