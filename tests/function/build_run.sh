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

prepare_environment() {
    # TODO: make this auto-detect things
    export CTI_TESTS_LAUNCHER_ARGS="-n4 --ntasks-per-node=2 --mpi=cray_shasta"

    return 0
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
            echo "Configuring with whitebox settings..."
            # export CTI_LAUNCHER_NAME=/opt/cray/pe/snplauncher/default/bin/mpiexec; just load the module
            # module load cray snplauncher
            export CTI_WLM_IMPL=generic
            export MPICH_SMP_SINGLE_COPY_OFF=0
        else
            echo "Configuring non-whitebox launcher settings..."
        fi
        if ! prepare_environment ; then
            echo "Failed to configure environment variables."
            return 1
        fi
        ./avocado-virtual-environment/avocado/bin/avocado run ./avocado_tests.py${ONE_TEST} --mux-yaml ./avocado_test_params.yaml
    else
        echo "No avocado environment setup. Cannot execute tests"
        return 1
    fi
}

###########################
#    BEGIN MAIN SCRIPT    #
###########################

# check that running this is feasible at all
if ! python3 --version > /dev/null ; then
    echo "python3 not found. Exiting..."
    exit 1
fi

# TODO
# calibrate script based on if running on whitebox
# if srun echo "" &> /dev/null ; then
#     echo "Calibrating script for non-whitebox"
#     ON_WHITEBOX=false
# else
#     echo "due to no srun assuming whitebox environment..."
# fi

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
    run_tests
fi

cd $START_DIR
