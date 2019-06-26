#!/bin/bash
       
valid_ssh(){
    if ssh -o PreferredAuthentications=publickey $HOSTNAME /bin/true exit ; then
        echo "SSH is properly setup"
    else
        echo "SSH is not properly setup..."
        echo "Attempting to diagnose issue..."
        if ! test -d ~/.ssh ; then
            echo "No .ssh directory. Creating one..."
            if mkdir ~/.ssh ; then
               echo "Successfully created ~/.ssh and required authroized_keys file"
            else
               echo "Failed to create directory."
               return 1
            fi
        fi
        if ! test -f ~/.ssh/authorized_keys ; then
            echo "No authorized keys file. Creating..."
            if touch ~/.ssh/authorized_keys ; then
                echo "Created authorized keys file."
            else
                echo "Failed to create file. Aborting..."
                return 1
            fi
        fi
        if ! test -f ~/.ssh/id_rsa.pub ; then
            echo "No public ssh key exists. Creating one..."
            YOUR_EMAIL="${USER}@cray.com"
            if ssh-keygen -t rsa -N "" -C "$YOUR_EMAIL" ; then
                echo "Public ssh key generated as ~/.ssh/id_rsa.pub with no password"
            else
                echo "Failed to generate public ssh key. Aborting..."
                return 1
            fi
        fi
        YOUR_KEY="$(cat ~/.ssh/id_rsa.pub)"
        if ! grep -Fxq "$YOUR_KEY" ~/.ssh/authorized_keys ; then
            echo "id_rsa.pub key not present in authorized keys. Attempting to append it..."
            if cat ~/.ssh/id_rsa.pub >> ~/.ssh/authorized_keys ; then
                echo "Key successfully added. All repair/setup steps completed..."
            else
                echo "Failed to append key to file. Check if file exists and permissions are correct. Aborting..."
                return 1
            fi
        fi
        echo "Attempting to verify SSH again..."
        if ssh -o PreferredAuthentications=publickey $HOSTNAME /bin/true exit ; then
            echo "SSH now properly configured. Resuming..."
        else
            echo "SSH failed. Possible the current id_rsa.pub key requires a password. New key required for vaild SSH use."
            return 1
        fi
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
if test -d ./avocado-virtual-environment ; then
    if valid_ssh ; then
        if create_mpi_app ; then
            run_tests
        fi
    fi
else
    if setup_avocado ; then
        if valid_ssh ; then
            if create_mpi_app ; then
                run_tests
            fi
        fi

    fi
fi

