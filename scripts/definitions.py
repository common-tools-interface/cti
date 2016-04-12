#!/usr/bin/env python
import sys, os, re, datetime, time, fnmatch, glob, commands, stat
from os import listdir
from os.path import isfile, join
from distutils.version import LooseVersion
from datetime import date
from datetime import datetime


primary_version     = "1.0.1" 
ver_two_dig         = ".".join([primary_version[0],primary_version[2]])
ver_two_dig_nodot   = "".join([primary_version[0],primary_version[2]]) 
pkgs_dir            = "/cray/css/pe/pkgs/cti/" + ver_two_dig
bld_dir             = "/cray/css/ulib/cti/builds/latest/install/"
arch                ="x86_64"
final		    = False

#if its a pre-release
if len(sys.argv) == 3:
  final = False
#else its a final release
elif len(sys.argv) >= 4 and (sys.argv[3].lower() == "-f"):
  final = True

def fetch_release(base_name, rev_num):
  rpm_tail = ".x86_64.rpm"
  itt_list = range(20)
  itt_list.reverse()
  rval = 0
  for i in itt_list:
    tmp_name = pkgs_dir + "/" + base_name + "-" + str(i) + "_" + "*" + "." + rev_num + rpm_tail
    if glob.glob(tmp_name):
      rval = i + 1
      break
  return str(rval)  

def fetch_os():
  if os.path.isfile("/etc/SuSE-release"):
    with open("/etc/SuSE-release", "r") as f:
      for line in f:
        if line.find("VERSION") != -1:
          os_sub = line.replace("VERSION = ", "")
          break
  elif os.path.isfile("/etc/redhat-release"):
    with open("/etc/redhat-release", "r") as f:
      for line in f:
        if line.find("CentOS release") != -1:
          os_sub = 'el6'
          break

  os_sub = os_sub.strip()
  if os_sub == "el6":
    return "_el6"
  elif os_sub == "12":
    return "_sles12"
  else:
    return ''


def release_date():
  cmd = 'source /cray/css/ulib/utilities/find_release_date.sh'
  str(os.system(cmd))
  status, rd = commands.getstatusoutput(cmd)
  return rd

def fetch_version(primary_ver, final, revision_number):

  if final:
    new_version_str = primary_ver
    print "this is the final package"
  else:
    #need to escape the decimal points in the version string
    primary_list = primary_ver.split(".")
    clean_version = "\.".join(primary_list)
    reg_str = clean_version + '\.[0-9]{1}'
    reg_x = re.compile(reg_str)
    pkgs_dir = "/cray/css/pe/pkgs/cti/" + ver_two_dig + "/"

    if os.path.exists(pkgs_dir) == 0:
      print "Creating " + pkgs_dir
      os.mkdir(pkgs_dir)
      cmd = 'chmod 775 ' +  pkgs_dir
      os.system(cmd)
    
    #only grab 4 digit version numbers / pre-releases
    onlyfiles = [ f for f in listdir(pkgs_dir) if isfile(join(pkgs_dir,f)) and reg_x.search(f) and f[-3:] == "rpm"]
    
    #if this is the first in this branch
    if len(onlyfiles) == 0:
      new_version_str = primary_ver + ".1"
    else: 
      #sort the file list, grab the last one
      convert = lambda text: int(text) if text.isdigit() else text
      alphanum_key = lambda key: [ convert(c) for c in re.split('([0-9]+)',key) ]
      onlyfiles.sort(key=alphanum_key)
      
      last_package = onlyfiles.pop()
      
      #determine the 4 digit version number
      last_package_ver = last_package.replace("cti-","")
      last_package_ver = last_package_ver.split("-")[0]
      version_list = last_package_ver.split(".")
   
      oldrev = last_package.split("_")[1]
      oldrev=oldrev.split(".")
      oldrev=oldrev[1]
      
      if oldrev != revision_number:
        #increment 4th digit
        #figure out the new last digit, regex above ensures 4 digits
        new_last_digit = str(int(version_list[3]) + 1)
        #replace the last digit of the version number
        version_list.pop()
        version_list.append(new_last_digit)
        new_version_str = ".".join(version_list)
      else:
        new_version_str = ".".join(version_list)

  return new_version_str
         

def fetch_revision():
  rev_command = "git rev-parse HEAD"
  rev_number  = os.popen(rev_command).read()
  rev_number  = rev_number.rstrip()

  dt_time = datetime.utcnow()
  dt_time = str(dt_time).replace(":","")
  dt_time = str(dt_time).replace("-","")
  dt_time = str(dt_time).replace(" ","")
  dt_time = str(dt_time)[0:12]

  rev_number = rev_number[0:13]
  timestmp_revnbr = str(dt_time) + "." + str(rev_number)

  os_ver = fetch_os()

  version = fetch_version(primary_version,final,rev_number)
  base_name = "cray-cti-" + version

  release_number = fetch_release(base_name, rev_number)

  package_name = base_name + "-" + release_number + "_" + timestmp_revnbr + os_ver
  rpm_name = package_name + "." + arch + ".rpm"

  contents = [timestmp_revnbr, version, release_number, rpm_name, os_ver]
  return contents


