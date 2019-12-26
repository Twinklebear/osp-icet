#!/bin/bash

get_physical_cores() {
	echo `grep "^cpu\\scores" /proc/cpuinfo | uniq | awk '{print $4}'`
}

set_ospray_env_vars() {
#	export I_MPI_PIN_RESPECT_CPUSET=0
#	export I_MPI_PIN_RESPECT_HCA=0
#	export I_MPI_PIN_DOMAIN=omp
#	export I_MPI_PIN_PROCESSOR_LIST=allcores
#	export OSPRAY_SET_AFFINITY=0
#	# If it's not set (i.e, we're not on Theta) figure out the node's phys. cores
#	# On theta this script runs on the mom node, and the core count would be wrong
#	if [ -z "$OSPRAY_THREADS"]; then
#		export OSPRAY_THREADS=$(get_physical_cores)
#	fi
#	export OMP_NUM_THREADS=$OSPRAY_THREADS
	source $EMBREE_DIR/embree-vars.sh
	export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:${OSPRAY_DIR}/build/install/lib64/
	export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:${OPENVKL_DIR}/build/install/lib64/
	export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:${OSPCOMMON_DIR}/build/install/lib/
	export MPICH_MAX_THREAD_SAFETY=multiple
}

set_ospray_env_vars

