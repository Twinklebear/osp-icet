#!/bin/bash

export IMAGE_SIZE_X=1024
export IMAGE_SIZE_Y=1024
export BENCH_ITERS=125

if [ "`hostname -d`" == "stampede2.tacc.utexas.edu" ]; then
  export OSPRAY_THREADS=68
  export MACHINE=stampede2
elif [ "`hostname -d`" == "ls5.tacc.utexas.edu" ]; then
  export OSPRAY_THREADS=20
  export MACHINE=ls5
fi

script_dir=$(dirname $(readlink -f $0))

compositors=(ospray) # icet)
node_counts=(2 4 8 16 32 64)
for c in "${compositors[@]}"; do
  export BENCH_COMPOSITOR=$c
  for i in "${node_counts[@]}"; do
    job_title="bench_${c}_${i}n_${IMAGE_SIZE_X}x${IMAGE_SIZE_Y}"
    sbatch -n $i -N $i --ntasks-per-node=1 -t 00:30:00 \
      -A OSPRay -p normal -S $OSPRAY_THREADS \
      -J $job_title -o ${job_title}.txt \
      ${script_dir}/run_compositing_bench.sh
  done
done

