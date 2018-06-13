#!/bin/bash

# Usage:
# ./submit_compositing_scaling <queue>

export IMAGE_SIZE_X=2048
export IMAGE_SIZE_Y=2048
export BENCH_ITERS=200
export OSPRAY_DP_API_TRACING=0

export CLUSTER_NAME="`hostname -d`"
if [ "$CLUSTER_NAME" == "stampede2.tacc.utexas.edu" ]; then
	export MACHINE=stampede2
	export TACC=true
	#export BUILD_DIR=$WORK/osp-icet/build-trace
	export BUILD_DIR=$WORK/osp-icet/build
	if [ "$1" == "skx-normal" ]; then
		export OSPRAY_THREADS=47
		export JOB_QUEUE=skx-normal
	else
		export OSPRAY_THREADS=67
		export JOB_QUEUE=normal
		#export JOB_QUEUE=development
		# Intel MPI progress thread - This gets rid of the performance
		# hiccups on KNL and makes our performance more stable
		#export I_MPI_ASYNC_PROGRESS=1
		#export I_MPI_ASYNC_PROGRESS_PIN=1
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
	export OSPRAY_THREADS=64
	export MACHINE=theta
fi

if [ -z "$BUILD_DIR" ]; then
	echo "Set the BUILD_DIR var!"
	exit 1
fi

script_dir=$(dirname $(readlink -f $0))

compositors=(ospray)
#node_counts=(2 4 8 16 32 64) # 128 256)
node_counts=(8)
for c in "${compositors[@]}"; do
	export BENCH_COMPOSITOR=$c
	for i in "${node_counts[@]}"; do
		job_title="bench_${c}_${i}n_${IMAGE_SIZE_X}x${IMAGE_SIZE_Y}"

		if [ -n "$TACC" ]; then
			export TACC_ARGS="-A OSPRay -p $JOB_QUEUE"
			job_title="${job_title}-$JOB_QUEUE"
		fi
		if [ -n "`command -v sbatch`" ]; then
			sbatch -n $i -N $i --ntasks-per-node=1 -t 00:05:00 \
				$TACC_ARGS \
				-S $OSPRAY_THREADS \
				-J $job_title -o ${job_title}-%j.txt \
				${script_dir}/run_compositing_bench.sh
		elif [ -n "`command -v qsub`" ]; then
			# A lot of crap for theta and qsub because cobalt is
			# dumb to pick up my environment
			THETA_JOB_NODES=$(( $i > 8 ? $i : 8 ))
			export THETA_USE_NODES=$i
			qsub -n $THETA_JOB_NODES -t 00:30:00 -A Viz_Support \
				-O ${job_title} \
				-q debug-cache-quad \
				--env "MACHINE=$MACHINE" \
				--env "OSPRAY_THREADS=$OSPRAY_THREADS" \
				--env "IMAGE_SIZE_X=$IMAGE_SIZE_X" \
				--env "IMAGE_SIZE_Y=$IMAGE_SIZE_Y" \
				--env "BENCH_ITERS=$BENCH_ITERS" \
				--env "BENCH_COMPOSITOR=$BENCH_COMPOSITOR" \
				--env "THETA_USE_NODES=$THETA_USE_NODES" \
				--env "BUILD_DIR=$BUILD_DIR" \
				${script_dir}/run_compositing_bench.sh				
		fi
	done
done

