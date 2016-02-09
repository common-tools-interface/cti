#!/usr/bin/env bash

# cti_slurm_util.sh: Get and format the layout of the current job step using the sattach command
# Usage: ./cti_slurm_util.sh <jobid> <stepid>

sattachOutput=$(sattach --layout $1.$2);
numTasks=$(echo "$sattachOutput" | awk 'FNR==2 {print $1}');
numNodes=$(echo "$sattachOutput" | awk 'FNR==2 {print $3}');
layout=$(echo "$sattachOutput" | awk '/Node/' | awk '{print $3":"$4":"$6}' | sed -r 's/[(](.+)[)],/\1/');
echo $numTasks $numNodes $layout;
