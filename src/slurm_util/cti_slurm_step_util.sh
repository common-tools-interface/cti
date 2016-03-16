#!/usr/bin/env bash

# cti_slurm_util.sh: Get and format the layout of the current job step using the sattach command
# Usage: ./cti_slurm_util.sh <jobid> <stepid>

ARG_HELP=0

if [ "$#" -lt "2" ] ; then
  echo -e "Usage: $0 [OPTIONS]...\n" \
  "Returns information about a job step.\n\n" \
  "\t-j, --jobid     slurm job id\n" \
  "\t-s, --stepid    slurm step id\n" \
  "\t-h, --help      Display this text and exit\n\n" \
  "Returns: task_cnt node_cnt host:tasks:tid ...\n" \
  "Parse with: %%d %%d %%s:%%d:%%d ...\n";
  exit 1;
fi

# read the options
TEMP=`getopt -o hj:s: --long help,jobid:,stepid: -- "$@"`
eval set -- "$TEMP"

# extract options and their arguments into variables.
while true ; do
    case "$1" in
        -h|--help) ARG_HELP=1 ; shift ;;
        -j|--jobid)
            case "$2" in
                "") shift 2 ;;
                *) ARG_JOBID=$2 ; shift 2 ;;
            esac ;;
        -s|--stepid)
            case "$2" in
                "") shift 2 ;;
                *) ARG_STEPID=$2 ; shift 2 ;;
            esac ;;
        --) shift ; break ;;
        *) echo "Internal error!" ; exit 1 ;;
    esac
done

sattachOutput=$(sattach --layout $ARG_JOBID.$ARG_STEPID);
numTasks=$(echo "$sattachOutput" | awk 'FNR==2 {print $1}');
numNodes=$(echo "$sattachOutput" | awk 'FNR==2 {print $3}');
layout=$(echo "$sattachOutput" | awk '/Node/' | awk '{print $3":"$4":"$6}' | sed -r 's/[(](.+)[)],/\1/');
echo $numTasks $numNodes $layout;
