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

#include <iostream>
#include <execvp_stream.hpp>

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

// sattach standard options
struct SattachArgv : Argv {
	SattachArgv(ProgramName const& name) : Argv(name) {}
	static constexpr ProgramName SattachArgv0 = { "nm" };

	static constexpr Parameter InputFilter  = { "--input-filter" };
	static constexpr Parameter OutputFilter = { "--output-filter" };
	static constexpr Parameter ErrorFilter  = { "--error-filter" };

	static constexpr Option PrependWithTaskLabel = { "--label" };
	static constexpr Option DisplayLayout        = { "--layout" };
	static constexpr Option RunInPty             = { "--pty" };
	static constexpr Option QuietOutput          = { "--quiet" };
	static constexpr Option VerboseOutput        = { "--verbose" };

	static constexpr Argument JobIdDotStepId     = {};
};

// Return: 
// num_PEs num_nodes host:num_here:PE0 ...
int
main(int argc, char **argv)
{

	std::string jobId, stepId;

	{ // parse options using getopt
		int opt_ind = 0;
		char c;

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
					jobId = std::string(optarg);
					
					break;
					
				case 's':
					if (optarg == NULL)
					{
						usage(argv[0]);
						return 1;
					}
					
					// This is the step id
					stepId = std::string(optarg);
					
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
		if (jobId.empty() || stepId.empty())
		{
			fprintf(stderr, "Missing jobid or stepid argument.\n");
			return 1;
		}
	}

	// create dotted argument for sattach
	std::string jobIdDotStepId;
	{ std::stringstream ss;
		ss << jobId << "." << stepId;
		jobIdDotStepId = ss.str();
	}

	// create sattach instance
	SattachArgv sattachArgv(SattachArgv::SattachArgv0);
	{ using SA = SattachArgv;
		sattachArgv.add(SA::DisplayLayout);
		sattachArgv.add(SA::JobIdDotStepId, jobIdDotStepId);
	}

	// create sattach output capture object
	ExecvpOutput sattachOutput("sattach", sattachArgv);

	/* sattach layout format:
	Job step layout:
	  {numTasks} tasks, {numNodes} nodes ({hostname}...)
	
	  Node {nodeNum} ({hostname}), {numPEs} task(s): PE_0 {PE_i }...
	*/

	/* expected output format:
	numTasks numNodes {hostname:numPEs:PE_0 }...
	*/

	// "Job step layout:"
	if (auto layoutHeader = sattachOutput.optional_getline()) {
		if (layoutHeader->compare("Job step layout:")) {
			throw std::runtime_error(
				std::string("sattach layout: wrong format: ") + *layoutHeader);
		}
	} else {
		throw std::runtime_error("sattach layout: wrong format: expected header");
	}

	// "  {numTasks} tasks, {numNodes} nodes ({hostname}...)"
	if (auto layoutSummary = sattachOutput.optional_getline()) {
		// split the summary line
		std::string numTasks, numNodes;
		std::tie(numTasks, std::ignore, numNodes) =
			split::string<3>(split::removeLeadingWhitespace(layoutSummary.value()));

		// output
		std::cout << numTasks << " " << numNodes << " ";
	} else {
		throw std::runtime_error("sattach layout: wrong format: expected summary");
	}

	// seperator line
	sattachOutput.optional_getline();

	// "  Node {nodeNum} ({hostname}), {numPEs} task(s): PE_0 {PE_i }..."
	while (auto nodeLayout = sattachOutput.optional_getline()) {
		// split the summary line
		std::string nodeNum, hostname, numPEs, pe_0;
		std::tie(std::ignore, nodeNum, hostname, numPEs, std::ignore, pe_0) =
			split::string<6>(split::removeLeadingWhitespace(nodeLayout.value()));

		// remove parens and comma from hostname
		hostname = hostname.substr(1, hostname.length() - 3);

		// output
		std::cout << hostname << ":" << numPEs << ":" << pe_0 << " ";
	}

	// wait for sattach to finish and cleanup
	sattachOutput.getExitStatus();
	std::cout << std::endl << std::flush;
	
	return 0;
}

