#!/bin/bash

export IMAGE_SIZE_X=2048
export IMAGE_SIZE_Y=2048
export BENCH_ITERS=200

if [ "`hostname -d`" == "stampede2.tacc.utexas.edu" ]; then
	export OSPRAY_THREADS=68
	export MACHINE=stampede2
	export TACC=true
elif [ "`hostname -d`" == "ls5.tacc.utexas.edu" ]; then
	export OSPRAY_THREADS=20
	export MACHINE=ls5
	export TACC=true
elif [ "`hostname`" == "wopr.sci.utah.edu" ]; then
	export OSPRAY_THREADS=32
	export MACHINE=wopr
fi

script_dir=$(dirname $(readlink -f $0))

compositors=(ospray icet)
node_counts=(16) # 32 64)
for c in "${compositors[@]}"; do
	export BENCH_COMPOSITOR=$c
	for i in "${node_counts[@]}"; do
		job_title="bench_${c}_${i}n_${IMAGE_SIZE_X}x${IMAGE_SIZE_Y}"
		if [ -n "$TACC" ]; then
			export $TACC_ARGS="-A OSPRay -p normal"
		fi
		sbatch -n $i -N $i --ntasks-per-node=1 -t 01:00:00 \
			$TACC_ARGS \
			-S $OSPRAY_THREADS \
			-J $job_title -o ${job_title}.txt \
			${script_dir}/run_compositing_bench.sh
	done
done

