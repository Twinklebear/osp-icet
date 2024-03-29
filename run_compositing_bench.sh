#!/bin/bash

module restore deps-ospray-dev

if [ -n "$WORK_DIR" ]; then
	echo "Changing to $WORK_DIR"
	cd $WORK_DIR
fi

echo "Script dir = $REPO_ROOT"
source $REPO_ROOT/set_ospray_vars.sh

# Only one of these will be non-empty
JOBID="${SLURM_JOBID}${COBALT_JOBID}"
NPROCS="${SLURM_NNODES}${COBALT_PARTSIZE}"

compositors=(dfb icet)
for c in "${compositors[@]}"; do
	export LOGFILE="bench-${c}-${NPROCS}n-${SLURM_JOB_PARTITION}-${JOBID}.txt"
	export JOB_PREFIX="${SLURM_JOB_PARTITION}-${NPROCS}-${JOBID}"
    if [ -n "$LOG_PREFIX" ]; then
        export LOGFILE="${LOG_PREFIX}-${LOGFILE}"
        export JOB_PREFIX="${LOG_PREFIX}-${JOB_PREFIX}"
    fi

	ibrun ${BUILD_DIR}/osp_icet \
        -$c \
        -prefix ${JOB_PREFIX} \
        ${JSON_CONFIG} \
        -no-output > ${LOGFILE} 2>&1
done

