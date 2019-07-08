#!/bin/bash

########################################################
# This script is designed to create an avocado testing #
# environment, ensure a proper SSH setup, then execute #
# all functional tests defined in ./avocado_tests.py   #
########################################################

#Contains python modules. Changed for whitebox running.
PYTHON=python3
PIP=pip
VENV=venv
ON_WHITEBOX=true

setup_python() { #!WB setup check
    if ! test -f ~/.local/bin/pip ; then
        echo "No pip detected. Installing..."
        if ! curl https://bootstrap.pypa.io/get-pip.py -o get-pip.py ; then
            echo "Failed to download pip setup script. Aborting..."
            return 1
        fi
        if ! python get-pip.py --user ; then
            echo "Failed to run pip installer. Aborting..."
            return 1
        fi
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

valid_ssh(){
    if ! ../scripts/validate_ssh.sh ; then
        return 1
    fi
}

create_venv() {
    if [ "$ON_WHITEBOX" = true ] ; then
        $PYTHON -m venv avocado
    else
        $VENV avocado
    fi
    if test -d ./avocado ; then
        return 0
    fi
    return 1
}

install_additional_plugins() {
    if [ "$ON_WHITEBOX" = true ] ; then
        #Install all desired whitebox plugins.
        if ! pip install avocado-framework-plugin-loader-yaml ; then
            return 1
        fi
    else
        #Install all desired non-whitebox plugins
        if ! pip install avocado-framework-plugin-loader-yaml ; then
            return 1
        fi
        return 0
    fi
}


setup_avocado() {
    echo "Creating avocado environment using $PYTHON"
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
                            $PYTHON ../avo_config.py $PWD $PYTHON_VERSION
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

run_tests() {
    if test -d ./avocado-virtual-environment ; then
        echo "Valid avocado virtual environment for testing..."
        if ! module load cray-snplauncher ; then
            echo "Failed to load cray-snplauncher. Aborting testing..."
            return 1
        fi
        # check if not running on a whitebox and if so load different parameters TODO: Add different configs and expand list
        if [ "$ON_WHITEBOX" = true ] ; then
            echo "Configuring with Whitebox settings..."
            export MPICH_SMP_SINGLE_COPY_OFF=0
            export CRAY_CTI_DIR=$PWD/../../install
            export CRAY_CTI_LAUNCHER_NAME=/opt/cray/pe/snplauncher/default/bin/mpiexec
            export CRAY_CTI_WLM=generic
        else
            echo "srun exists so configuring non-whitebox launcher settings..."
            export MPICH_SMP_SINGLE_COPY_OFF=0
        fi
        if [ "$ON_WHITEBOX" = true ] ; then
            ./avocado-virtual-environment/avocado/bin/avocado run ./avocado_tests.py --mux-yaml ./avocado_test_params.yaml
        else
            ./avocado-virtual-environment/avocado/bin/avocado run ./avocado_tests.py --mux-yaml
        fi
    else
        echo "No avocado environment setup. Cannot execute tests"
        return 1
    fi
}

create_mpi_app() {
    if test -f ./basic_hello_mpi ; then
        echo "MPI app already compiled..."
    else
        echo "Compiling basic mpi application for use in testing script..."
        module load cray-snplauncher  
        module load modules/3.2.11.2
        module load PrgEnv-cray
        module load cray-mpich/7.7.8
        export MPICH_SMP_SINGLE_COPY_OFF=0
        if cc -o basic_hello_mpi hello_mpi.c ; then
            echo "Application successfully compiled into 'basic_hello_mpi'"
        else
            echo "Failed to compile MPI application. Aborting..."
            return 1
        fi
    fi
}

###########################
#    BEGIN MAIN SCRIPT    #
###########################

# check that running this is feasible at all
if [ ! python3 --version > /dev/null ] && [ ! python --version > /dev/null ]  ; then
    echo "No valid python install found. Exiting..."
    exit 1
fi

# calibrate script based on if running on whitebox
if srun echo "" > /dev/null ; then
    if ! setup_python ; then
        echo "Failed to setup valid whitebox python environment"
        exit 1
    fi
    echo "Calibrating script for non-whitebox"
    PYTHON=python
    PIP=~/.local/bin/pip
    VENV=~/.local/bin/virtualenv
    ON_WHITEBOX=false
else
    echo "due to no srun assuming whitebox environment..."
fi

# check that the path to tests/function relative to current
# directory was provided. If not simply exit.
START_DIR=$PWD
cd ${1:-./}

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
    if create_mpi_app ; then
         run_tests
    fi
fi

cd $START_DIR
