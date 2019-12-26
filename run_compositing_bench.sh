#!/bin/bash

module restore

if [ -n "$WORK_DIR" ]; then
	echo "Changing to $WORK_DIR"
	cd $WORK_DIR
fi

REPO_ROOT=`git rev-parse --show-toplevel`
echo "Script dir = $REPO_ROOT"
source $REPO_ROOT/set_ospray_vars.sh

# Only one of these will be non-empty
JOBID="${SLURM_JOBID}${COBALT_JOBID}"
NPROCS="${SLURM_NNODES}${COBALT_PARTSIZE}"

compositors=(dfb icet)
for c in "${compositors[@]}"; do
	export LOGFILE="bench_${c}_${NPROCS}n.txt"
	export BENCH_ARGS="-$c ${JSON_CONFIG}"

	ibrun ./osp_icet $BENCH_ARGS > $LOGFILE 2>&1
done

