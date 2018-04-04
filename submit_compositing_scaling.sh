#!/bin/bash

export IMAGE_SIZE_X=1024
export IMAGE_SIZE_Y=1024
export BENCH_ITERS=150

export CLUSTER_NAME="`hostname -d`"
if [ "$CLUSTER_NAME" == "stampede2.tacc.utexas.edu" ]; then
	export MACHINE=stampede2
	export TACC=true
	if [ "$1" == "skx-normal" ]; then
		export OSPRAY_THREADS=48
		export JOB_QUEUE=skx-normal
	else
		export OSPRAY_THREADS=68
		export JOB_QUEUE=normal
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

script_dir=$(dirname $(readlink -f $0))

compositors=(ospray icet)
node_counts=(2 4 8 16 32 64)
for c in "${compositors[@]}"; do
	export BENCH_COMPOSITOR=$c
	for i in "${node_counts[@]}"; do
		job_title="bench_${c}_${i}n_${IMAGE_SIZE_X}x${IMAGE_SIZE_Y}"

		if [ -n "$TACC" ]; then
			export TACC_ARGS="-A OSPRay -p $JOB_QUEUE"
		fi
		if [ -n "`command -v sbatch`" ]; then
			sbatch -n $i -N $i --ntasks-per-node=1 -t 00:20:00 \
				$TACC_ARGS \
				-S $OSPRAY_THREADS \
				-J $job_title -o ${job_title}.txt \
				${script_dir}/run_compositing_bench.sh
		elif [ -n "`command -v qsub`" ]; then
			# A lot of crap for theta and qsub because cobalt is
			# dumb to pick up my environment
			THETA_JOB_NODES=$(( $i > 8 ? $i : 8 ))
			export THETA_USE_NODES=$i
			qsub -n $THETA_JOB_NODES -t 00:30:00 -A Viz_Support \
				-o ${job_title}.txt \
				--env "MACHINE=$MACHINE" \
				--env "OSPRAY_THREADS=$OSPRAY_THREADS" \
				--env "IMAGE_SIZE_X=$IMAGE_SIZE_X" \
				--env "IMAGE_SIZE_Y=$IMAGE_SIZE_Y" \
				--env "BENCH_ITERS=$BENCH_ITERS" \
				--env "BENCH_COMPOSITOR=$BENCH_COMPOSITOR" \
				--env "THETA_USE_NODES=$THETA_USE_NODES" \
				${script_dir}/run_compositing_bench.sh				
		fi
	done
done

