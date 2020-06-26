#!/bin/bash

# these are meant to be changed by hand if the defaults don't work.

# launcher args used if slurm is detected
SLURM_TESTS_LAUNCHER_ARGS="-n4 --ntasks-per-node=2"
# launcher args used if alps is detected
ALPS_TESTS_LAUNCHER_ARGS="-n4 -N2"
# launcher args used if mpiexec is detected
SSH_TESTS_LAUNCHER_ARGS="-n4 --ntasks-per-node=2"

################################################################################

echo "Doing system specific setup..."

detect_wlm() {
    # try to detect a wlm
    if module load wlm_detect &> /dev/null && wlm_detect &> /dev/null; then
        wlm_detect
        return 0
    fi

    if srun --version &> /dev/null; then
        echo "SLURM"
        return 0
    fi

    if aprun --version &> /dev/null; then
        echo "ALPS"
        return 0
    fi

    if mpiexec -h &> /dev/null; then
        echo "SSH"
        return 0
    fi

    echo "Couldn't detect a WLM."
    return 1
}

get_launcher() {
    if [[ $# -ne 1 ]]; then
        echo "usage: get_launcher <WLM>"
        return 1
    fi

    case $1 in
    "SLURM")
        echo "srun"
        ;;
    "ALPS")
        echo "aprun"
        ;;
    "SSH")
        echo "mpiexec"
        ;;
    *)
        echo "Unknown WLM!"
        return 1
        ;;
    esac
}

get_arguments() {
    if [[ $# -ne 1 ]]; then
        echo "usage: get_arguments <WLM>"
        return 1
    fi

    case $WLM in
    "SLURM")
        echo "$SLURM_TESTS_LAUNCHER_ARGS"
        ;;
    "ALPS")
        echo "$ALPS_TESTS_LAUNCHER_ARGS"
        ;;
    "SSH")
        echo "$SSH_TESTS_LAUNCHER_ARGS"
        ;;
    *)
        echo "Unknown WLM!"
        return 1
        ;;
    esac
}

if detect_wlm &> /dev/null; then
    WLM=$(detect_wlm)
    echo Detected $WLM.
else
    echo "Failed to detect wlm."
    exit 1
fi

if get_launcher $WLM &> /dev/null; then
    export CTI_TESTS_LAUNCHER=$(get_launcher $WLM)
    echo Launcher is $CTI_TESTS_LAUNCHER
else
    echo "Failed to get launcher."
    exit 1
fi

if get_arguments $WLM &> /dev/null; then
    export CTI_TESTS_LAUNCHER_ARGS=$(get_arguments $WLM)
    echo Launcher args are \""$CTI_TESTS_LAUNCHER_ARGS"\"
else
    echo "Failed to get launcher args"
    exit 1
fi
