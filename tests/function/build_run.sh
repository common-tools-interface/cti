#!/bin/bash

########################################################
# This script is designed to create an avocado testing #
# environment, ensure a proper SSH setup, then execute #
# all functional tests defined in ./avocado_tests.py   #
########################################################
      
valid_ssh(){
   if ! ../../scripts/validate_ssh.sh ; then
       return 1
   fi
}


setup_avocado() {
    #Create avocado environment
    if mkdir avocado-virtual-environment && cd avocado-virtual-environment ; then
        if python3 -m venv avocado ; then

            #Install avocado plugin
            if . $PWD/avocado/bin/activate ; then
                if pip install avocado-framework ; then
 
                #Install additional avocado plugins
                pip install avocado-framework-plugin-loader-yaml
                #Configure avocado
                    if mkdir job-results ; then
                        PYTHON_VERSION="$(ls $PWD/avocado/lib/)" 
                        python3 ../avo_config.py $PWD $PYTHON_VERSION
                        cd ../
                    else
                        echo "Failed to create job-results directory"
                        echo "Job-results will now be stored in ~/avocado"
                    fi
                else
                    echo "Pip failed to install avocado-framework"
                    echo "Cleaning up..."
                    cd ../
                    rm -r avocado-virtual-environment
                    return 0
                fi
            else
                echo "Failed to activate python virtual environment"
                echo "Cleaning up..."
                cd ../
                rm -r avocado-virtual-environment
                return 0
            fi
        else
            echo "Failed to create python virtual environment"
            echo "Cleaning up..."
            cd ../
            rm -r avocado-virtual-environment
            return 0
        fi
    else
        echo "Failed to create avocado-virtual-environment directory"
        return 0
    fi   
}

run_tests() {
    if test -d ./avocado-virtual-environment ; then
        echo "Valid avocado virtual environment for testing..."
        module load cray-snplauncher
        export MPIEXEC_TIMEOUT=10
        export MPICH_SMP_SINGLE_COPY_OFF=0
        export CRAY_CTI_DIR=$PWD/../../install
        export CRAY_CTI_LAUNCHER_NAME=/opt/cray/pe/snplauncher/default/bin/mpiexec
        export CRAY_CTI_WLM=generic
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
        export MPICH_SMP_SINGLE_COPY_OFF=0
        if cc -o basic_hello_mpi hello_mpi.c ; then
            echo "Application successfully compiled into 'basic_hello_mpi'"
        else
            echo "Failed to compile MPI application. Aborting..."
            exit 1
        fi
    fi
}

###########################
#    BEGIN MAIN SCRIPT    #
###########################

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
