/*********************************************************************************\
 * alps_transfer.c - A interface to the alps toolhelper functions. This provides
 *		   a tool developer with an easy to use interface to transfer
 *		   binaries, shared libraries, and files to the compute nodes
 *		   associated with an aprun pid. This can also be used to launch
 *		   tool daemons on the compute nodes in an automated way.
 *
 * © 2011-2012 Cray Inc.  All Rights Reserved.
 *
 * Unpublished Proprietary Information.
 * This unpublished work is protected to trade secret, copyright and other laws.
 * Except as permitted by contract or express written permission of Cray Inc.,
 * no part of this work or its content may be used, reproduced or disclosed
 * in any form.
 *
 * $HeadURL$
 * $Date$
 * $Rev$
 * $Author$
 *
 *********************************************************************************/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "alps/libalps.h"

#include "alps_transfer.h"
#include "alps_application.h"
#include "ld_val.h"

#include "useful/useful.h"

int
sendCNodeExec(pid_t aprunPid, char *fstr, char **args, char **env, int dbg)
{
	appEntry_t *	app_ptr;	// pointer to the appEntry_t object associated with the provided aprun pid
	const char *	errmsg;		// errmsg that is possibly returned by call to alps_launch_tool_helper
	char *			fullname;	// full path name of the executable to launch as a tool daemon
	char *			realname;	// realname (lacking path info) of the executable
	char *			args_flat;	// flattened args array to pass to the toolhelper call
	char *			cpy;		// temporary cpy var used in creating args_flat
	char *			launcher;	// full path name of the daemon launcher application
	char **			lib_array;	// the returned list of strings containing the required libraries by the executable
	char **			tmp;		// temporary pointer used to iterate through lists of strings
	size_t			len, env_base_len;	// len vars used in creating the args_flat string

	// sanity check
	if (aprunPid <= 0 || fstr == (char *)NULL)
		return 1;
		
	// try to find an entry in the my_apps array for the aprunPid
	if ((app_ptr = findApp(aprunPid)) == (appEntry_t *)NULL)
	{
		// aprun pid not found in the global my_apps array
		// so lets create a new appEntry_t object for it
		if ((app_ptr = newApp(aprunPid)) == (appEntry_t *)NULL)
		{
			// we failed to create a new appEntry_t entry - catastrophic failure
			return 1;
		}
	}
	
	// first we should ensure that the executable file exists and convert it to its fullpath name
	if ((fullname = pathFind(fstr, NULL)) == (char *)NULL)
	{
		fprintf(stderr, "Could not locate the specified executable in PATH.\n");
		return 1;
	}
	
	// next just grab the real name (without path information) of the executable
	if ((realname = pathToName(fullname)) == (char *)NULL)
	{
		fprintf(stderr, "Could not convert the fullname to realname.\n");
		return 1;
	}
	
	// search the shipped_execs list for a duplicate filename
	if (!searchStringList(app_ptr->shipped_execs, realname))
	{
		// not found in list, so this is a unique file name
		
		// ship the executable to the compute nodes
		if ((errmsg = alps_launch_tool_helper(app_ptr->alpsInfo.apid, app_ptr->alpsInfo.pe0Node, 1, 0, 1, &fullname)) != (const char *)NULL)
		{
			// we failed to ship the file to the compute nodes for some reason - catastrophic failure
			fprintf(stderr, "%s\n", errmsg);
			return 1;
		}
		
		// add filename to the saved list
		if (addString(app_ptr->shipped_execs, realname))
		{
			// we failed to insert fstr into the stringList but was able to ship - serious failure 
			// for now we should record catastrophic failure fixme in the future
			return 1;
		}
		
		// call the ld_val interface to determine if this executable has any dso requirements
		lib_array = ld_val(fullname);
		
		// If this executable has dso requirements. We need to ship them over.
		tmp = lib_array;
		// ensure the are actual entries before dereferencing tmp
		if (tmp != (char **)NULL)
		{
			while (*tmp != (char *)NULL)
			{
				if (sendCNodeLibrary(aprunPid, *tmp++))
				{
					// if we return with non-zero status, catastrophic failure occured in the lib transfer
					return 1;
				}
			}
		
			// cleanup the returned ld_val array
			tmp = lib_array;
			while (*tmp != (char *)NULL)
			{
				free(*tmp++);
			}
			free(lib_array);
		}
		
		// now we need to create the flattened argv string for the actual call to the wrapper
		// this is passed through the toolhelper
		// The options passed MUST correspond to the options defined in the daemon_launcher program.
		
		// Find the location of the daemon launcher program
		if ((launcher = pathFind(ALPS_LAUNCHER, NULL)) == (char *)NULL)
		{
			fprintf(stderr, "Could not locate the launcher application in PATH.\n");
			return 1;
		}
		
		// determine the length of the argv[0] and -b (binary) argument
		len = strlen(launcher) + strlen(" -b ") + strlen(realname);
		
		// iterate through the env array and determine its total length
		env_base_len = strlen(" -e "); 
		tmp = env;
		// ensure the are actual entries before dereferencing tmp
		if (tmp != (char **)NULL)
		{
			while (*tmp != (char *)NULL)
			{
				len += env_base_len + strlen(*tmp++);
			}
		}
		
		// if debug is on, add the len of the debug switch
		if (dbg)
		{
			len += strlen(" --debug");
		}
		
		// add the length of the "--" terminator to end the opt parsing
		len += strlen(" --");
		
		// iterate through the args array and determine its length
		tmp = args;
		// ensure the are actual entries before dereferencing tmp
		if (tmp != (char **)NULL)
		{
			while (*tmp != (char *)NULL)
			{
				len += strlen(" ") + strlen(*tmp++) + strlen(" ");
			}
		}
		
		// add one for the null terminator
		++len;
		
		// malloc space for this string buffer
		if ((args_flat = malloc(len)) == (void *)NULL)
		{
			// malloc failed
			return 1;
		}
		
		// start creating the flattened args string
		snprintf(args_flat, len, "%s -b %s", launcher, realname);
		
		// add each of the env arguments
		tmp = env;
		// ensure the are actual entries before dereferencing tmp
		if (tmp != (char **)NULL)
		{
			while (*tmp != (char *)NULL)
			{
				// we need a temporary copy of the args_flat string so far
				cpy = strdup(args_flat);
				snprintf(args_flat, len, "%s -e %s", cpy, *tmp++);
				free(cpy);
			}
		}
		
		// add the debug switch if debug is on
		if (dbg)
		{
			cpy = strdup(args_flat);
			snprintf(args_flat, len, "%s --debug", cpy);
			free(cpy);
		}
		
		// add the "options" terminator
		cpy = strdup(args_flat);
		snprintf(args_flat, len, "%s --", cpy);
		free(cpy);
		
		// add each of the args from the args array
		tmp = args;
		// ensure the are actual entries before dereferencing tmp
		if (tmp != (char **)NULL)
		{
			while (*tmp != (char *)NULL)
			{
				cpy = strdup(args_flat);
				snprintf(args_flat, len, "%s %s ", cpy, *tmp++);
				free(cpy);
			}
		}
		
		// Done. We now have a flattened args string
		// We can launch the tool daemon onto the compute nodes now.
		if ((errmsg = alps_launch_tool_helper(app_ptr->alpsInfo.apid, app_ptr->alpsInfo.pe0Node, 1, 1, 1, &args_flat)) != NULL)
		{
			// we failed to launch the launcher on the compute nodes for some reason - catastrophic failure
			fprintf(stderr, "%s\n", errmsg);
			return 1;
		}
		
		// cleanup our memory
		free(args_flat);
	}
	
	// cleanup our memory
	free(fullname);
	free(realname);
	
	// filename has already been shipped - enforce uniqueness requirements and silently fail
	return 0;
}

int
sendCNodeBinary(pid_t aprunPid, char *fstr)
{
	appEntry_t *	app_ptr;	// pointer to the appEntry_t object associated with the provided aprun pid
	const char *	errmsg;		// errmsg that is possibly returned by call to alps_launch_tool_helper
	char *			fullname;	// full path name of the file to ship to the compute nodes
	char *			realname;	// realname (lacking path info) of the file
	char **			lib_array;	// the returned list of strings containing the required libraries by the executable
	char **			tmp;		// temporary pointer used to iterate through lists of strings
	
	// sanity check
	if (aprunPid <= 0 || fstr == (char *)NULL)
		return 1;

	// try to find an entry in the my_apps array for the aprunPid
	if ((app_ptr = findApp(aprunPid)) == (appEntry_t *)NULL)
	{
		// aprun pid not found in the global my_apps array
		// so lets create a new appEntry_t object for it
		if ((app_ptr = newApp(aprunPid)) == (appEntry_t *)NULL)
		{
			// we failed to create a new appEntry_t entry - catastrophic failure
			return 1;
		}
	}
	
	// first we should ensure that the file exists and convert it to its fullpath name
	if ((fullname = pathFind(fstr, NULL)) == (char *)NULL)
	{
		fprintf(stderr, "Could not locate the specified file in PATH.\n");
		return 1;
	}
	
	// next just grab the real name (without path information) of the executable
	if ((realname = pathToName(fullname)) == (char *)NULL)
	{
		fprintf(stderr, "Could not convert the fullname to realname.\n");
		return 1;
	}
	
	// search the shipped_files list for a duplicate filename
	if (!searchStringList(app_ptr->shipped_execs, realname))
	{
		// not found in list, so this is a unique file name
		
		// ship the file to the compute nodes
		if ((errmsg = alps_launch_tool_helper(app_ptr->alpsInfo.apid, app_ptr->alpsInfo.pe0Node, 1, 0, 1, &fullname)) != (const char *)NULL)
		{
			// we failed to ship the file to the compute nodes for some reason - catastrophic failure
			fprintf(stderr, "%s\n", errmsg);
			return 1;
		}
		
		// add filename to the saved list
		if (addString(app_ptr->shipped_execs, realname))
		{
			// we failed to insert fstr into the stringList but was able to ship - serious failure 
			// for now we should record catastrophic failure fixme in the future
			return 1;
		}
		
		// call the ld_val interface to determine if this executable has any dso requirements
		lib_array = ld_val(fullname);
		
		// If this executable has dso requirements. We need to ship them over.
		tmp = lib_array;
		// ensure the are actual entries before dereferencing tmp
		if (tmp != (char **)NULL)
		{
			while (*tmp != (char *)NULL)
			{
				if (sendCNodeLibrary(aprunPid, *tmp++))
				{
					// if we return with non-zero status, catastrophic failure occured in the lib transfer
					return 1;
				}
			}
			
			tmp = lib_array;
			while (*tmp != (char *)NULL)
			{
				free(*tmp++);
			}
			free(lib_array);
		}
	}
	
	// cleanup memory
	free(fullname);
	free(realname);
	
	// filename has already been shipped - enforce uniqueness requirements and silently fail
	return 0;
}

int
sendCNodeLibrary(pid_t aprunPid, char *fstr)
{
	appEntry_t *	app_ptr;	// pointer to the appEntry_t object associated with the provided aprun pid
	const char *	errmsg;		// errmsg that is possibly returned by call to alps_launch_tool_helper
	char *			fullname;	// full path name of the file to ship to the compute nodes
	char *			realname;	// realname (lacking path info) of the file

	// sanity check
	if (aprunPid <= 0 || fstr == (char *)NULL)
		return 1;
		
	// try to find an entry in the my_apps array for the aprunPid
	if ((app_ptr = findApp(aprunPid)) == (appEntry_t *)NULL)
	{
		// aprun pid not found in the global my_apps array
		// so lets create a new appEntry_t object for it
		if ((app_ptr = newApp(aprunPid)) == (appEntry_t *)NULL)
		{
			// we failed to create a new appEntry_t entry - catastrophic failure
			return 1;
		}
	}
		
	// first we should ensure that the library exists and convert it to its fullpath name
	if ((fullname = libFind(fstr, NULL)) == (char *)NULL)
	{
		fprintf(stderr, "Could not locate the specified file in PATH.\n");
		return 1;
	}
	
	// next just grab the real name (without path information) of the library
	if ((realname = pathToName(fullname)) == (char *)NULL)
	{
		fprintf(stderr, "Could not convert the fullname to realname.\n");
		return 1;
	}
	
	// TODO: We need to create a way to ship conflicting libraries. Since most libraries are sym links to
	// their proper version, name collisions are possible. In the future, the launcher should be able to handle
	// this by pointing its LD_LIBRARY_PATH to a custom directory containing the conflicting lib.
	
	// search the shipped_libs list for a duplicate filename
	if (!searchStringList(app_ptr->shipped_libs, realname))
	{
		// not found in list, so this is a unique file name
		
		// ship the library to the compute nodes
		if ((errmsg = alps_launch_tool_helper(app_ptr->alpsInfo.apid, app_ptr->alpsInfo.pe0Node, 1, 0, 1, &fullname)) != (const char *)NULL)
		{
			// we failed to ship the library to the compute nodes for some reason - catastrophic failure
			fprintf(stderr, "%s\n", errmsg);
			return 1;
		}
		
		// add filename to the saved list
		if (addString(app_ptr->shipped_libs, realname))
		{
			// we failed to insert fstr into the stringList but was able to ship - serious failure 
			// for now we should record catastrophic failure fixme in the future
			return 1;
		}
	}
	
	// cleanup memory
	free(fullname);
	free(realname);
	
	// filename has already been shipped - enforce uniqueness requirements and silently fail
	return 0;
}

int
sendCNodeFile(pid_t aprunPid, char *fstr)
{
	appEntry_t *	app_ptr;	// pointer to the appEntry_t object associated with the provided aprun pid
	const char *	errmsg;		// errmsg that is possibly returned by call to alps_launch_tool_helper
	char *			fullname;	// full path name of the file to ship to the compute nodes
	char *			realname;	// realname (lacking path info) of the file

	// sanity check
	if (aprunPid <= 0 || fstr == (char *)NULL)
		return 1;

	// try to find an entry in the my_apps array for the aprunPid
	if ((app_ptr = findApp(aprunPid)) == (appEntry_t *)NULL)
	{
		// aprun pid not found in the global my_apps array
		// so lets create a new appEntry_t object for it
		if ((app_ptr = newApp(aprunPid)) == (appEntry_t *)NULL)
		{
			// we failed to create a new appEntry_t entry - catastrophic failure
			return 1;
		}
	}
	
	// first we should ensure that the file exists and convert it to its fullpath name
	if ((fullname = pathFind(fstr, NULL)) == (char *)NULL)
	{
		fprintf(stderr, "Could not locate the specified file in PATH.\n");
		return 1;
	}
	
	// next just grab the real name (without path information) of the executable
	if ((realname = pathToName(fullname)) == (char *)NULL)
	{
		fprintf(stderr, "Could not convert the fullname to realname.\n");
		return 1;
	}
	
	// search the shipped_files list for a duplicate filename
	if (!searchStringList(app_ptr->shipped_files, realname))
	{
		// not found in list, so this is a unique file name
		
		// ship the file to the compute nodes
		if ((errmsg = alps_launch_tool_helper(app_ptr->alpsInfo.apid, app_ptr->alpsInfo.pe0Node, 1, 0, 1, &fullname)) != (const char *)NULL)
		{
			// we failed to ship the file to the compute nodes for some reason - catastrophic failure
			fprintf(stderr, "%s\n", errmsg);
			return 1;
		}
		
		// add filename to the saved list
		if (addString(app_ptr->shipped_files, realname))
		{
			// we failed to insert fstr into the stringList but was able to ship - serious failure 
			// for now we should record catastrophic failure fixme in the future
			return 1;
		}
	}
	
	// cleanup memory
	free(fullname);
	free(realname);
	
	// filename has already been shipped - enforce uniqueness requirements and silently fail
	return 0;
}

