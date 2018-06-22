#!/bin/bash

# Usage:
# ./submit_compositing_scaling <queue>

export IMAGE_SIZE_X=2048
export IMAGE_SIZE_Y=2048
export BENCH_ITERS=200
export OSPRAY_DP_API_TRACING=1

export CLUSTER_NAME="`hostname -d`"
if [ "$CLUSTER_NAME" == "stampede2.tacc.utexas.edu" ]; then
	export MACHINE=stampede2
	export TACC=true
	#export BUILD_DIR=$WORK/osp-icet/build-trace
	export BUILD_DIR=$WORK/osp-icet/build
	# Intel MPI progress thread - This gets rid of the performance
	# hiccups on KNL and makes our performance more stable
	export I_MPI_ASYNC_PROGRESS=1
	#export I_MPI_ASYNC_PROGRESS_PIN=1
	if [ -z "$1" ]; then
		echo "A queue is required for stampede2!"
		exit 1
	elif [ "$1" == "skx-normal" ]; then
		export OSPRAY_THREADS=48
		export JOB_QUEUE=skx-normal
	else
		echo "Assuming $1 is KNL queue"
		export OSPRAY_THREADS=68
		export JOB_QUEUE=$1
	fi
elif [ "$CLUSTER_NAME" == "ls5.tacc.utexas.edu" ]; then
	export OSPRAY_THREADS=20
	export MACHINE=ls5
	export TACC=true
	export JOB_QUEUE=normal
elif [ "$CLUSTER_NAME" == "maverick.tacc.utexas.edu" ]; then
	export OSPRAY_THREADS=20
	export MACHINE=maverick
	export TACC=true
	export JOB_QUEUE=vis
elif [ "`hostname`" == "wopr.sci.utah.edu" ]; then
	export OSPRAY_THREADS=32
	export MACHINE=wopr
	export JOB_QUEUE=normal
elif [ "`hostname | head -c 5`" == "theta" ]; then
	export OSPRAY_THREADS=63
	export MACHINE=theta
	#export MPICH_NEMESIS_ASYNC_PROGRESS=MC
	#export MPICH_GNI_ASYNC_PROGRESS_TIMEOUT=0
	export MPICH_MAX_THREAD_SAFETY=multiple
fi

if [ -z "$BUILD_DIR" ]; then
	echo "Set the BUILD_DIR var!"
	exit 1
fi

script_dir=$(dirname $(readlink -f $0))

node_counts=(2) # 4 8 16 32 64 128)
#node_counts=(128)
for i in "${node_counts[@]}"; do
	job_title="bench_${i}n_${IMAGE_SIZE_X}x${IMAGE_SIZE_Y}"

	if [ -n "$TACC" ]; then
		export TACC_ARGS="-A OSPRay -p $JOB_QUEUE"
		job_title="${job_title}-$JOB_QUEUE"
	fi
	if [ -n "`command -v sbatch`" ]; then
		sbatch -n $i -N $i --ntasks-per-node=1 -t 00:15:00 \
			$TACC_ARGS \
			--export=all \
			-J $job_title -o ${job_title}-%j.txt \
			${script_dir}/run_compositing_bench.sh
	elif [ -n "`command -v qsub`" ]; then
		# A lot of crap for theta and qsub because cobalt is
		# dumb to pick up my environment
		#THETA_JOB_NODES=$(( $i > 8 ? $i : 8 ))
		# TODO: Theta increased to 128 node min, so instead
		# we'd really want to run all the 2-128 node benchmarks
		# with a single job
		THETA_JOB_NODES=$i
		qsub -n $THETA_JOB_NODES -t 00:30:00 -A UINTAH_aesp \
			-O ${job_title} \
			--env "MACHINE=$MACHINE" \
			--env "OSPRAY_THREADS=$OSPRAY_THREADS" \
			--env "IMAGE_SIZE_X=$IMAGE_SIZE_X" \
			--env "IMAGE_SIZE_Y=$IMAGE_SIZE_Y" \
			--env "BENCH_ITERS=$BENCH_ITERS" \
			--env "BUILD_DIR=$BUILD_DIR" \
			--env "THETA_JOBNAME=$job_title" \
			--env "OSPRAY_DP_API_TRACING=$OSPRAY_DP_API_TRACING" \
			${script_dir}/run_compositing_bench.sh				
	fi
done

