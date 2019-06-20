import sys
import configparser

#sys.argv[1] contains current directory path
#sys.argv[2] contains version avocda used for install

#read in avocado configs
avocado_dir_path = sys.argv[1]
config = configparser.ConfigParser()
avocado_config_path = avocado_dir_path + "/avocado/lib/" + sys.argv[2] + "/site-packages/avocado/etc/avocado/avocado.conf"
config.read(avocado_config_path)

#customize configurations
config["datadir.paths"]["logs_dir"] = avocado_dir_path + "/job-results"

#write changes to config file
with open (avocado_config_path, 'w') as acp:
	config.write(acp)
