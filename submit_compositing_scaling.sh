#!/bin/bash

export IMAGE_SIZE_X=2048
export IMAGE_SIZE_Y=2048
export BENCH_ITERS=250

if [ "`hostname -d`" == "stampede2.tacc.utexas.edu" ]; then
  echo "Setting OSPRAY_THREADS=68 for KNL"
  export OSPRAY_THREADS=68
fi

script_dir=$(dirname $(readlink -f $0))

compositors=(ospray icet)
node_counts=(2 4 8 16 32 64)
for c in "${compositors[@]}"; do
  export BENCH_COMPOSITOR=$c
  for i in "${node_counts[@]}"; do
    job_title="bench_${c}_${i}n_${IMAGE_SIZE_X}x${IMAGE_SIZE_Y}"
    sbatch -n $i -N $i --ntasks-per-node=1 -t 00:15:00 \
      -A OSPRay -p normal \
      -J $job_title -o ${job_title}.txt \
      ${script_dir}/run_compositing_bench.sh
  done
done

