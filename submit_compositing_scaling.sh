#!/bin/bash

# Usage:
# ./submit_compositing_scaling <queue>

if [ -z "$IMAGE_SIZE_X" ] || [ -z "$IMAGE_SIZE_Y" ]; then
	export IMAGE_SIZE_X=2048
	export IMAGE_SIZE_Y=2048
else
	echo "Using image settings from command line ${IMAGE_SIZE_X}x${IMAGE_SIZE_Y}"
fi

export BENCH_ITERS=150
export OSPRAY_DP_API_TRACING=1
export CLUSTER_NAME="`hostname -d`"

if [[ "$CLUSTER_NAME" == *"tacc"* ]]; then
	export TACC=true
fi

if [ "$CLUSTER_NAME" == "stampede2.tacc.utexas.edu" ]; then
	export MACHINE=stampede2
	if [ -z "$1" ]; then
		echo "A queue is required for stampede2!"
		exit 1
	fi
	if [ "$1" == "skx-normal" ]; then
		export JOB_QUEUE=skx-normal
	else
		echo "Assuming $1 is KNL queue"
		export JOB_QUEUE=$1
	fi
elif [ "$CLUSTER_NAME" == "ls5.tacc.utexas.edu" ]; then
	export MACHINE=ls5
	export JOB_QUEUE=normal
elif [ "$CLUSTER_NAME" == "maverick.tacc.utexas.edu" ]; then
	export MACHINE=maverick
	export JOB_QUEUE=vis
elif [ "`hostname`" == "wopr.sci.utah.edu" ]; then
	export MACHINE=wopr
	export JOB_QUEUE=normal
elif [ "`hostname | head -c 5`" == "theta" ]; then
	export OSPRAY_THREADS=64
	export MACHINE=theta
	if [ -n "$1" ]; then
		export JOB_QUEUE=$1
	else
		echo "Using default queue for Theta"
		export JOB_QUEUE="default"
	fi
fi

if [ -z "$BUILD_DIR" ]; then
	echo "Set the BUILD_DIR var!"
	exit 1
fi

export SCRIPT_DIR=$(dirname $(readlink -f $0))

#export TACC_TRACING=1

if [ "$MACHINE" == "theta" ]; then
	if [ "$JOB_QUEUE" == "default" ]; then
		node_counts=(128 256 512 1024)
	elif [ "$JOB_QUEUE" == "debug-cache-quad" ]; then
		node_counts=(4)
	fi
elif [ "$MACHINE" == "stampede2" ]; then
	if [ "$JOB_QUEUE" == "normal" ]; then
		#node_counts=(4 8 16 32 64 128 256)
		node_counts=(32)
	elif [ "$JOB_QUEUE" == "large" ]; then
		node_counts=(1024)
	elif [ "$JOB_QUEUE" == "skx-normal" ]; then
		node_counts=(4 8 16 32 64 128)
	elif [ "$JOB_QUEUE" == "skx-large" ]; then
		node_counts=(256)
	elif [ "$JOB_QUEUE" == "development" ]; then
		node_counts=(2)
	fi
else
	echo "Unrecognized machine, unsure on node counts to scale!"
	exit 1
fi

for i in "${node_counts[@]}"; do
	if [ -n "$PREFIX" ]; then
		export job_title="${PREFIX}-bench_${i}n_${IMAGE_SIZE_X}x${IMAGE_SIZE_Y}"
	else
		export job_title="bench_${i}n_${IMAGE_SIZE_X}x${IMAGE_SIZE_Y}"
	fi

	if [ -n "$TACC" ]; then
		export TACC_ARGS="-A OSPRay -p $JOB_QUEUE -x c464-021"
		job_title="${job_title}-$JOB_QUEUE"
	fi
	if [ -n "`command -v sbatch`" ]; then
		sbatch -n $i -N $i --ntasks-per-node=1 -t 00:05:00 \
			$TACC_ARGS \
			--export=all \
			-J $job_title -o ${job_title}-%j.txt \
			${SCRIPT_DIR}/run_compositing_bench.sh
	elif [ -n "`command -v qsub`" ]; then
		# A lot of crap for theta and qsub because cobalt is
		# dumb to pick up my environment
		THETA_JOB_NODES=$i
		TIME="00:30:00"
		if [ "$IMAGE_SIZE_X" == "8192" ]; then
			TIME="01:00:00"
		fi

		qsub -n $THETA_JOB_NODES -t $TIME -A UINTAH_aesp \
			-O ${job_title} \
			-q $JOB_QUEUE \
			--env "MACHINE=$MACHINE" \
			--env "OSPRAY_THREADS=$OSPRAY_THREADS" \
			--env "IMAGE_SIZE_X=$IMAGE_SIZE_X" \
			--env "IMAGE_SIZE_Y=$IMAGE_SIZE_Y" \
			--env "BENCH_ITERS=$BENCH_ITERS" \
			--env "BUILD_DIR=$BUILD_DIR" \
			--env "THETA_JOBNAME=$job_title" \
			--env "OSPRAY_DP_API_TRACING=$OSPRAY_DP_API_TRACING" \
			--env "PREFIX=$PREFIX" \
			--env "SCRIPT_DIR=$SCRIPT_DIR" \
			${SCRIPT_DIR}/run_compositing_bench.sh				
	fi
done

