#!/bin/bash

#Create avocado environment
mkdir avocado-virtual-environment
cd avocado-virtual-environment
python3 -m venv avocado

#Install avocado plugins
. $PWD/avocado/bin/activate
pip install avocado-framework
#pip install avocado-framework-plugin-result-html
pip install avocado-framework-plugin-loader-yaml
pip install avocado-framework-plugin-varianter-yaml-to-mux

#Configure avocado
mkdir job-results
PYTHON_VERSION="$(ls $PWD/avocado/lib/)" 
python3 ../avo_config.py $PWD $PYTHON_VERSION
