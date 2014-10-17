/******************************************************************************\
 * slurm_util.c - A utility that interfaces with the slurm API to return useful
 *                information about a job step.
 *
 * Copyright 2014 Cray Inc. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 ******************************************************************************/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <errno.h>
#include <getopt.h>
#include <inttypes.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>

#include <slurm/slurm.h>

const struct option long_opts[] = {
			{"jobid",		required_argument,	0, 'j'},
			{"stepid",		required_argument,	0, 's'},
			{"help",		no_argument,		0, 'h'},
			{0, 0, 0, 0}
			};
			
static void
usage(char *name)
{
	fprintf(stdout, "Usage: %s [OPTIONS]...\n", name);
	fprintf(stdout, "Returns information about a job step.\n\n");
	
	fprintf(stdout, "\t-j, --jobid     slurm job id\n");
	fprintf(stdout, "\t-s, --stepid    slurm step id\n");
	fprintf(stdout, "\t-h, --help      Display this text and exit\n\n");

	fprintf(stdout, "Returns: task_cnt node_cnt host:tasks:tid ...\n");
	fprintf(stdout, "Parse with: %%d %%d %%s:%%d:%%d ...\n");
}

// Return: 
// num_PEs num_nodes host:num_here:PE0 ...
int
main(int argc, char **argv)
{
	int								opt_ind = 0;
	int								j_arg = 0;
	int								s_arg = 0;
	int								c,i;
	char *							eptr;
	slurm_step_layout_t *			step_layout = NULL;
	hostlist_t						host_list;
	char *							host;
	uint32_t						job_id = 0;
	uint32_t						step_id = 0;

	// we require at least 1 argument beyond argv[0]
	if (argc < 2)
	{
		usage(argv[0]);
		return 1;
	}
	
	// process longopts
	while ((c = getopt_long(argc, argv, "j:s:h", long_opts, &opt_ind)) != -1)
	{
		switch (c)
		{
			case 0:
				// if this is a flag, do nothing
				break;
			
			case 'j':
				if (optarg == NULL)
				{
					usage(argv[0]);
					return 1;
				}
				
				// This is the job id
				errno = 0;
				job_id = (uint32_t)strtol(optarg, &eptr, 10);
				
				// check for error
				if ((errno == ERANGE && (job_id == LONG_MAX || job_id == LONG_MIN))
						|| (errno != 0 && job_id == 0))
				{
					perror("strtol");
					return 1;
				}
				
				// check for invalid input
				if (eptr == optarg || *eptr != '\0')
				{
					fprintf(stderr, "Invalid --jobid argument.\n");
					return 1;
				}
				
				j_arg = 1;
				
				break;
				
			case 's':
				if (optarg == NULL)
				{
					usage(argv[0]);
					return 1;
				}
				
				// This is the step id
				errno = 0;
				step_id = (uint32_t)strtol(optarg, &eptr, 10);
				
				// check for error
				if ((errno == ERANGE && (step_id == LONG_MAX || step_id == LONG_MIN))
						|| (errno != 0 && step_id == 0))
				{
					perror("strtol");
					return 1;
				}
				
				// check for invalid input
				if (eptr == optarg || *eptr != '\0')
				{
					fprintf(stderr, "Invalid --stepid argument.\n");
					return 1;
				}
				
				s_arg = 1;
				
				break;
				
			case 'h':
				usage(argv[0]);
				return 0;
				
			default:
				usage(argv[0]);
				return 1;
		}
	}
	
	// ensure we got the correct number of args
	if (!s_arg || !j_arg)
	{
		fprintf(stderr, "Missing jobid or stepid argument.\n");
		return 1;
	}

	// get the layout for this job step
	if ((step_layout = slurm_job_step_layout_get(job_id, step_id)) == NULL)
	{
		fprintf(stderr, "slurm_job_step_layout_get() failed.\n");
		return 1;
	}
	
	// create a hostlist based on this layout
	if ((host_list = slurm_hostlist_create(step_layout->node_list)) == NULL)
	{
		fprintf(stderr, "slurm_hostlist_create() failed.\n");
		slurm_job_step_layout_free(step_layout);
		return 1;
	}
	
	// ensure the number in the host_list jive with our step layout
	if (slurm_hostlist_count(host_list) != step_layout->node_cnt)
	{
		fprintf(stderr, "Node count mismatch.\n");
		slurm_hostlist_destroy(host_list);
		slurm_job_step_layout_free(step_layout);
		return 1;
	}
	
	// print out the initial information
	fprintf(stdout, "%d %d", step_layout->task_cnt, step_layout->node_cnt);
	
	// loop over the host list and print out information based on each host
	i = 0;
	while ((host = slurm_hostlist_shift(host_list)) != NULL)
	{
		fprintf(stdout, " %s:%d:%d", host, step_layout->tasks[i], *step_layout->tids[i]);
		// increment i counter
		++i;
	}
	
	// cleanup
	slurm_hostlist_destroy(host_list);
	slurm_job_step_layout_free(step_layout);
	
	fflush(stdout);
	
	return 0;
}

