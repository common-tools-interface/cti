#!/usr/bin/env python
import sys, os, re, datetime, time, fnmatch, glob, commands, stat
from os import listdir
from os.path import isfile, join
from distutils.version import LooseVersion
from datetime import date
from datetime import datetime

product_dir           = "/cray/css/pe/pkgs/cti"
bld_dir               = ""
arch                  = "x86_64"
final                 = False
pkgs_dir              = ""
two_digit_version     = ""
ver_two_dig_nodot     = ""
three_digit_version   = ""
revision_number       = ""
version               = ""
release_number        = ""
rpm_name              = ""
osver                 = ""
revision_number       = ""

for arg in sys.argv:
  if arg == '-f':
    final = True

def fetch_release(base_name, rev_num,pkgs_dir):
  rpm_tail = ".x86_64.rpm"
  itt_list = range(20)
  itt_list.reverse()
  rval = 0
  for i in itt_list:
    tmp_name = pkgs_dir + "/" + base_name + "-" + "*" + "." + rev_num + "-" + str(i) + osver + rpm_tail
    if glob.glob(tmp_name):
      rval = i + 1
      break
  return str(rval)  

def fetch_os():
  global bld_dir
  if os.path.isfile("/etc/SuSE-release"):
    with open("/etc/SuSE-release", "r") as f:
      for line in f:
        if line.find("VERSION") != -1:
          os_sub = line.replace("VERSION = ", "")
          break
  elif os.path.isfile("/etc/redhat-release"):
    with open("/etc/redhat-release", "r") as f:
      for line in f:
        if line.find("CentOS ") != -1:
          os_sub = 'el7'
          break

  os_sub = os_sub.strip()
  if os_sub == "el7":
    bld_dir = "/cray/css/ulib/cti/builds_cs/latest/install/"
    return ".el7"
  elif os_sub == "12":
    bld_dir = "/cray/css/ulib/cti/builds_xc/latest/install/"
    return ".sles12"
  elif os_sub == "11":
    bld_dir = "/cray/css/ulib/cti/builds_xc/latest/install/"
    return ".sles11"


def release_date():
  cmd = 'source /cray/css/ulib/utilities/find_release_date.sh'
  str(os.system(cmd))
  status, rd = commands.getstatusoutput(cmd)
  return rd

def fetch_packageDir():
  #get a list of sub directories representing the products 2 digit release version
  onlyDirs = os.listdir(product_dir)
  if len(onlyDirs) == 0:
    print "There are no product subdirectories."
    exit(1)
  else:
    #get the latest directory
    #sort the file list, grab the last one
    convert = lambda text: int(text) if text.isdigit() else text
    alphanum_key = lambda key: [ convert(c) for c in re.split('([0-9]+)',key) ]
    onlyDirs.sort(key=alphanum_key)
    last_Dir = onlyDirs.pop()

  return last_Dir


def fetch_latest_version(final,revision,pkgs_dir):
  #get a list of sub directories representing the products 2 digit release version
  onlyfiles = glob.glob(pkgs_dir + "/*.rpm")

  if len(onlyfiles) == 0:
    #there aren't any pkgs for this new two digit version yet.
     #build the version from the 2 digit version
     if final:
       version = two_digit_ver + "." + 0-0
     else:
       version = two_digit_ver + "." + 0.0-0
  else:
    #get the latest package
    #if os.path.exists(pkgs_dir) == 0:
	#print "Creating " + pkgs_dir
        #os.mkdir(pkgs_dir)
        #cmd = 'chmod 775 ' +  pkgs_dir
	#os.system(cmd)

    #sort the file list, grab the last one
    convert = lambda text: int(text) if text.isdigit() else text
    alphanum_key = lambda key: [ convert(c) for c in re.split('([0-9]+)',key) ]
    onlyfiles.sort(key=os.path.getmtime)
    last_package = onlyfiles.pop()
    #determine the 4 digit version number
    rpmName = last_package.split("/")[7].replace("cray-cti-","")
    version = rpmName.split("-")[0]
    version_list = version.split(".")
    #get old revision
    oldrev = rpmName.split("-")[1]
    oldrev=oldrev.split(".")
    oldrev=oldrev[1]

    #determine if latest package was a pre-release or a release
    if len(version) == 5:
	latest_ver = "release"
    elif len(version) == 7:
        latest_ver = "pre-release"

    print "The latest package created was a " + latest_ver

    #create new version based on old version
    if latest_ver == "release":

      if final == False:
          #then there is a new revision
          #get third digit and increment
          third_digit = version_list[2]

          #increment third digit
          third_digit = str(int(third_digit) + 1)
          
          #postpend a .1
	  version_list.pop()
          version_list.append(third_digit)
          version_list.append("1")
          new_version_str = ".".join(version_list)
      elif final == True:
        #then repackaging, same revision
        new_version_str = ".".join(version_list)
       

    elif latest_ver == "pre-release":

         #if branch is master
      if final == False:
         if oldrev != revision:
           #increment 4th digit
           #figure out the new last digit
           new_last_digit = str(int(version_list[3]) + 1)

           #replace the last digit of the version number
           version_list.pop()
           version_list.append(new_last_digit)
           new_version_str = ".".join(version_list)
	 else:
           new_version_str = ".".join(version_list)
      elif final == True:
	   #remove 4th digit
	   version_list.pop()
	   new_version_str = ".".join(version_list)
    
    #new_version_str = new_version_str.split("/")[7]  
    return new_version_str
    

def fetch_newData():
  
  global two_digit_version
  global timestmp_revnbr
  global pkgs_dir
  global three_digit_version
  global version
  global release_number
  global rpm_name
  global osver
  global ver_two_dig_nodot

  rev_command = "git rev-parse HEAD"
  rev_number  = os.popen(rev_command).read()
  rev_number  = rev_number.rstrip()

  branch = "git rev-parse --abbrev-ref HEAD"
  branch = os.popen(branch).read()
  branch = branch.rstrip()
  print "Packaging from: " + branch
  if final == True and branch == 'master':
    print "You indicated this should be a final build, but you are packaging from " + branch
    print "Exiting. . ."
    exit(1)
  
  time_command = "git log -s -1 --format=%ci $tm | tail -n1 | cut -d' ' -f1-2"
  dt_time = os.popen(time_command).read()
  dt_time = dt_time.rstrip()
  dt_time = str(dt_time).replace(":","")
  dt_time = str(dt_time).replace("-","")
  dt_time = str(dt_time).replace(" ","")
  dt_time = str(dt_time)[0:12]

  rev_number = rev_number[0:13]
  timestmp_revnbr = str(dt_time) + "." + str(rev_number)
  
  osver = fetch_os()
  two_digit_version = fetch_packageDir()
  pkgs_dir = product_dir + "/" + two_digit_version
  version = fetch_latest_version(final,rev_number,pkgs_dir)
  three_digit_version = version[0:5]
  ver_two_dig_nodot = version[0] + version[2]

  base_name = "cray-cti-" + version
  release_number = fetch_release(base_name, rev_number,pkgs_dir)
  package_name = base_name + "-" + timestmp_revnbr + "-" + release_number + osver
  rpm_name = package_name + "." + arch + ".rpm"

  return

