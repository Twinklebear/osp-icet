#!/bin/bash

export IMAGE_SIZE_X=1024
export IMAGE_SIZE_Y=1024
export BENCH_ITERS=200

export CLUSTER_NAME="`hostname -d`"
if [ "$CLUSTER_NAME" == "stampede2.tacc.utexas.edu" ]; then
	export OSPRAY_THREADS=68
	export MACHINE=stampede2
	export TACC=true
  export JOB_QUEUE=normal
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
fi

script_dir=$(dirname $(readlink -f $0))

compositors=(ospray icet)
node_counts=(2 4 8 16 32)
for c in "${compositors[@]}"; do
	export BENCH_COMPOSITOR=$c
	for i in "${node_counts[@]}"; do
		job_title="bench_${c}_${i}n_${IMAGE_SIZE_X}x${IMAGE_SIZE_Y}"
		if [ -n "$TACC" ]; then
			export TACC_ARGS="-A OSPRay -p $JOB_QUEUE"
		fi
		sbatch -n $i -N $i --ntasks-per-node=1 -t 01:00:00 \
			$TACC_ARGS \
			-S $OSPRAY_THREADS \
			-J $job_title -o ${job_title}.txt \
			${script_dir}/run_compositing_bench.sh
	done
done

