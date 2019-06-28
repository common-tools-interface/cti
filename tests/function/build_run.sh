#!/bin/bash

########################################################
# This script is designed to create an avocado testing #
# environment, ensure a proper SSH setup, then execute #
# all functional tests defined in ./avocado_tests.py   #
########################################################

#Contains python to use
PYTHON=python3

valid_ssh(){
   if ! ../scripts/validate_ssh.sh ; then
       return 1
   fi
}


setup_avocado() {
    echo "Creating avocado environment using $PYTHON"
    #Create avocado environment
    if mkdir avocado-virtual-environment && cd avocado-virtual-environment ; then
        if $PYTHON -m venv avocado ; then

            #Install avocado plugin
            if . $PWD/avocado/bin/activate ; then
                if pip install avocado-framework ; then
 
                    #Install additional avocado plugins
                    if pip install avocado-framework-plugin-loader-yaml ; then
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
        if [ "$HOSTNAME" = "jupiter" ] || [ "$HOSTNAME" = "kay" ] || [ "$HOSTNAME" = "pepis" ] || [ "$HOSTNAME" = "monster" ] ; then
            echo "Configuring non-whitebox launcher settings..."
            export MPIEXEC_TIMEOUT=30
            export MPICH_SMP_SINGLE_COPY_OFF=0
            #TODO add more if nessecary
        else
            echo "Configuring whitebox launcher settings..."
            export MPIEXEC_TIMEOUT=30
            export MPICH_SMP_SINGLE_COPY_OFF=0
            export CRAY_CTI_DIR=$PWD/../../install
            export CRAY_CTI_LAUNCHER_NAME=/opt/cray/pe/snplauncher/default/bin/mpiexec
            export CRAY_CTI_WLM=generic
        fi
        ./avocado-virtual-environment/avocado/bin/avocado run ./avocado_tests.py --mux-yaml ./avocado_test_params.yaml
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
if ! python3 --version > /dev/null ; then
    echo "No valid python install found. Exiting..."
    exit 1
fi

# check that the path to tests/function relative to current
# directory was provided. If not simply exit.
START_DIR=$PWD
cd ${1:-./}

# if not in the proper directory. check by comparing against file in functional tests
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
