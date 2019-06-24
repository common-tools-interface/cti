#!/bin/bash

#Create avocado environment
if mkdir avocado-virtual-environment && cd avocado-virtual-environment ; then
    if python3 -m venv avocado ; then

        #Install avocado plugin
        if . $PWD/avocado/bin/activate ; then
            if pip install avocado-framework ; then

            #Install additional avocado plugins
            #pip install avocado-framework-plugin-loader-yaml
            #pip install avocado-framework-plugin-varianter-yaml-to-mux

            #Configure avocado
                if mkdir job-results ; then
                    PYTHON_VERSION="$(ls $PWD/avocado/lib/)" 
                    python3 ../avo_config.py $PWD $PYTHON_VERSION
                else
                    echo "Failed to create job-results directory"
                    echo "Job-results will now be stored in ~/avocado"
                fi
            else
                echo "Pip failed to install avocado-framework"
                echo "Cleaning up..."
	        cd ../
                rm -r avocado-virtual-environment
            fi
        else
            echo "Failed to activate python virtual environment"
            echo "Cleaning up..."
            cd ../
            rm -r avocado-virtual-environment
        fi
    else
        echo "Failed to create python virtual environment"
        echo "Cleaning up..."
        cd ../
        rm -r avocado-virtual-environment
    fi
else
    echo "Failed to create avocado-virtual-environment directory"
fi
