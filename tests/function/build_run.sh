#!/bin/bash

########################################################
# This script is designed to create an avocado testing #
# environment, ensure a proper SSH setup, then execute #
# all functional tests defined in ./avocado_tests.py   #
########################################################

#DIRECTORY RELATED VALUES
START_DIR=$PWD
FUNCTION_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 &&pwd )"
EXEC_DIR=$FUNCTION_DIR

#RUNNING AS PART OF NIGHTLY TESTING?
NIGHTLY_TEST=false

# take an argument as a single test to run
if [[ $# -eq 1 ]] ; then
    ONE_TEST=":"$1
fi


########################################################
# This function is designed to ensure python is        #
# properly setup on a non-whitebox system. These       #
# systems tend to be missing things like PIP and this  #
# will install them properly so the script can run     #
########################################################

setup_python() {
    if ! test -f ~/.local/bin/pip ; then
        echo "No pip detected. Installing..."
        if ! curl https://bootstrap.pypa.io/get-pip.py -o get-pip.py ; then
            echo "Failed to download pip setup script. Aborting..."
            return 1
        fi
        if ! python3 get-pip.py --user ; then
            echo "Failed to run pip installer. Aborting..."
            return 1
        fi
        rm get-pip.py
    fi
    echo "Pip install is valid..."
    if ! test -f ~/.local/bin/virtualenv ; then
        echo "Virtual environment module not installed. Installing..."
        if ! ~/.local/bin/pip install --user virtualenv ; then
            echo "Failed to install virtual environment module. Aborting..."
            return 1
        fi
    fi
    echo "VENV install is valid..."
    echo "Python setup is valid..."
    return 0
}

########################################################
# This function runs the validate_ssh script to ensure #
# that SSH is properly configured to run functional    #
# tests. If not it simply aborts as MPI apps need a    #
# valid SSH setup.                                     #
########################################################

valid_ssh(){
    ../scripts/validate_ssh.sh
    return $?
}

########################################################
# This function creates the python virtual environment #
# that the avocado plugin will be installed into. This #
# provides better containment for any plugins that are #
# needed for testing.                                  #
########################################################

create_venv() {
    ~/.local/bin/virtualenv avocado
    return $?
}

########################################################
# This function installs any additional avocado        #
# plugins that are desired for testing.                #
########################################################

install_additional_plugins() {
    if ! pip install avocado-framework-plugin-loader-yaml ; then
        return 1
    fi

    if ! pip install avocado-framework-plugin-result-html ; then
        return 1
    fi
    
    return 0
}

########################################################
# This function creates the avocado virtual environment#
# that everything will be installed in. In the event   #
# of failure the environment will be cleaned up unless #
# it has reached a far enough state to be functional   #
# even with certain errors.                            #
########################################################

setup_avocado() {
    echo "Creating avocado environment using python3"
    #Create avocado environment
    if mkdir avocado-virtual-environment && cd avocado-virtual-environment ; then
        if create_venv ; then

            #Install avocado plugin
            if . $PWD/avocado/bin/activate ; then
                if pip install avocado-framework ; then

                    #Install additional avocado plugins
                    if install_additional_plugins ; then
                    #Configure avocado
                        if mkdir job-results ; then
                            local PYTHON_VERSION="$(ls $PWD/avocado/lib/)"
                            python3 ../avo_config.py $PWD $PYTHON_VERSION
                        else
                            echo "Failed to create job-results directory"
                            echo "Job-results will now be stored in ~/avocado"
                        fi
                        cd ../
                        return 0
                    else
                        echo "Pip failed to install required avocado yaml loader"
                    fi
                else
                    echo "Pip failed to install avocado-framework"
                fi
            else
                echo "Failed to activate python virtual environment"
            fi
        else
            echo "Failed to create python virtual environment"
        fi
        echo "Cleaning up..."
        cd ../
        rm -r avocado-virtual-environment
    else
        echo "Failed to create avocado-virtual-environment directory"
    fi
    return 1
}

########################################################
# This function executes the current set of functional #
# tests. It configures the environment beforehand      #
# based on whether or not it is being ran on or off of #
# a white-box.                                         #
########################################################

run_tests() {
    if test -d ./avocado-virtual-environment ; then
        echo "Valid avocado virtual environment for testing..."

        # check if not running on a whitebox and if so load different parameters
        if [ "$ON_WHITEBOX" = true ] ; then
            if [ "$NIGHTLY_TEST" = true ] ; then
                echo "Configuring with nightly testing whitebox settings..."
                export MPICH_SMP_SINGLE_COPY_OFF=0
                export CTI_LAUNCHER_NAME=/opt/cray/pe/snplauncher/default/bin/mpiexec
                export CTI_WLM_IMPL=generic
            else
                echo "Configuring with normal whitebox settings..."
                export MPICH_SMP_SINGLE_COPY_OFF=0
                # maybe consider checking CTI_INSTALL_DIR both for null and empty with:
                # : "${CTI_INSTALL_DIR:?Need to set CTI_INSTALL_DIR non-empty}"
                if [ -z "$CTI_INSTALL_DIR" ] ; then
                    export CTI_INSTALL_DIR=$PWD/../../install
                fi
                if [ ! -d "$CTI_INSTALL_DIR" ]; then
                    echo "CTI_INSTALL_DIR=$CTI_INSTALL_DIR not found. Cannot execute tests"
                    return 1
                fi
                export LD_LIBRARY_PATH=$CTI_INSTALL_DIR/lib:$LD_LIBRARY_PATH
                export CTI_LAUNCHER_NAME=/opt/cray/pe/snplauncher/default/bin/mpiexec
                export CTI_WLM_IMPL=generic
            fi
        else
            echo "srun exists so configuring non-whitebox launcher settings..."
            export MPICH_SMP_SINGLE_COPY_OFF=0
        fi
        ./avocado-virtual-environment/avocado/bin/avocado run ./avocado_tests.py${ONE_TEST} --mux-yaml ./avocado_test_params.yaml
    else
        echo "No avocado environment setup. Cannot execute tests"
        return 1
    fi
}

########################################################
# This function compiles a basic MPI app that simply   #
# prints hello world based on how many nodes it runs   #
# on. Due to its simplicity it is used as a basic test #
# to run various other functional tests on             #
########################################################

create_mpi_app() {
    if test -f ./basic_hello_mpi ; then
        echo "MPI app already compiled..."
    else
        echo "Compiling basic mpi application for use in testing script..."
        module load cray-snplauncher
        module load modules/3.2.11.2
        module load PrgEnv-cray
        module load cray-mpich/7.7.8
        if cc -o basic_hello_mpi hello_mpi.c ; then
            echo "Application successfully compiled into 'basic_hello_mpi'"
        else
            echo "Failed to compile MPI application. Aborting..."
            return 1
        fi
    fi
}
create_mpi_wait_app() {
    if test -f ./basic_hello_mpi_wait ; then
        echo "MPI wait app already compiled..."
    else
        echo "Compiling basic mpi wait application for use in testing script..."
        module load cray-snplauncher
        module load modules/3.2.11.2
        module load PrgEnv-cray
        module load cray-mpich/7.7.8
        if cc -o basic_hello_mpi_wait hello_mpi_wait.c ; then
            echo "Application successfully compiled into 'basic_hello_mpi_wait'"
        else
            echo "Failed to compile MPI wait application. Aborting..."
            return 1
        fi
    fi
}

flags(){
    echo "Available flags:"
    echo "-h: display this"
    echo "-n: run nightly test  DEFAULT : $NIGHTLY_TEST"
    echo "-d: execution dir     DEFAULT : $EXEC_DIR"
    return 0
}

###########################
#    BEGIN MAIN SCRIPT    #
###########################

# check that amount of paramters passed in is valid
while getopts 'hnd:' flag; do
    case "${flag}" in
        h) flags
           exit 1 ;;
        n) NIGHTLY_TEST=true ;;
	d) EXEC_DIR=${OPTARG} ;;
    esac
done

# check that running this is feasible at all
if ! python3 --version > /dev/null ; then
    echo "python3 not found. Exiting..."
    exit 1
fi

# calibrate script based on if running on whitebox
if srun echo "" &> /dev/null ; then
    echo "Calibrating script for non-whitebox"
    ON_WHITEBOX=false
else
    echo "due to no srun assuming whitebox environment..."
fi

if ! setup_python ; then
    echo "Failed to setup valid whitebox python environment"
    exit 1
fi

# switch to the function test directory
cd $EXEC_DIR

# check if in proper directory by comparing against file in functional tests
if ! test -f ./avocado_tests.py ; then
    echo "Invalid path to functional tests directory provided"
    exit 1
fi

# test if avocado environment exists. If it does don't remake it.
if ! test -d ./avocado-virtual-environment ; then
    if ! setup_avocado ; then
        exit 1
    fi
fi
if valid_ssh ; then
    if create_mpi_app && create_mpi_wait_app; then
         run_tests
    fi
fi

cd $START_DIR
