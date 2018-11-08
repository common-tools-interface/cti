/******************************************************************************\
 * slurm_util.cpp - Helper functions that call slurm sattach to get
 *                  information about a job step.
 *
 * Copyright 2014 Cray Inc.	All Rights Reserved.
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

#include <iostream>
#include <string>

#include "useful/strong_argv.hpp"
#include "useful/string_split.hpp"
#include "useful/ExecvpOutput.hpp"

#include "slurm_util.h"

#include "ArgvDefs.hpp"

using Option    = Argv::Option;
using Parameter = Argv::Parameter;

slurmStepLayout_t *_cti_cray_slurm_getLayout(uint32_t job_id, uint32_t step_id) {
	// create dotted argument for sattach
	std::string jobIdDotStepId;
	{ std::stringstream ss;
		ss << job_id << "." << step_id;
		jobIdDotStepId = ss.str();
	}

	// create sattach instance
	OutgoingArgv<SattachArgv> sattachArgv("sattach");
	{ using SA = SattachArgv;
		sattachArgv.add(SA::DisplayLayout);
		sattachArgv.add(SA::Argument(jobIdDotStepId));
	}

	// create sattach output capture object
	ExecvpOutput sattachOutput("sattach", sattachArgv.get());
	std::istream& sattachStream(sattachOutput.stream());
	std::string sattachLine;

	// create layout container
	slurmStepLayout_t *layout = new slurmStepLayout_t;

	/* sattach layout format:
	Job step layout:
	  {numPEs} tasks, {numNodes} nodes ({hostname}...)
	
	  Node {nodeNum} ({hostname}), {numPEs} task(s): PE_0 {PE_i }...
	*/

	// "Job step layout:"
	if (std::getline(sattachStream, sattachLine)) {
		if (sattachLine.compare("Job step layout:")) {
			throw std::runtime_error(
				std::string("sattach layout: wrong format: ") + sattachLine);
		}
	} else {
		throw std::runtime_error("sattach layout: wrong format: expected header");
	}

	// "  {numPEs} tasks, {numNodes} nodes ({hostname}...)"
	if (std::getline(sattachStream, sattachLine)) {
		// split the summary line
		std::string numPEs, numNodes;
		std::tie(numPEs, std::ignore, numNodes) =
			split::string<3>(split::removeLeadingWhitespace(sattachLine));

		// fill out sattach layout
		layout->numPEs = std::stoi(numPEs);
		layout->numNodes = std::stoi(numNodes);
		layout->hosts = new slurmNodeLayout_t[layout->numNodes];
	} else {
		throw std::runtime_error("sattach layout: wrong format: expected summary");
	}

	// seperator line
	std::getline(sattachStream, sattachLine);

	// "  Node {nodeNum} ({hostname}), {numPEs} task(s): PE_0 {PE_i }..."
	for (int i = 0; std::getline(sattachStream, sattachLine); i++) {
		if (i >= layout->numNodes) {
			throw std::runtime_error("malformed sattach output: too many nodes!");
		}

		// split the summary line
		std::string nodeNum, hostname, numPEs, pe_0;
		std::tie(std::ignore, nodeNum, hostname, numPEs, std::ignore, pe_0) =
			split::string<6>(split::removeLeadingWhitespace(sattachLine));

		// remove parens and comma from hostname
		hostname = hostname.substr(1, hostname.length() - 3);

		// fill out node layout
		layout->hosts[i].host    = strdup(hostname.c_str());
		layout->hosts[i].PEsHere = std::stoi(numPEs);
		layout->hosts[i].firstPE = std::stoi(pe_0);
	}

	return layout;
}

void _cti_cray_slurm_freeLayout(slurmStepLayout_t *layout) {
	for (int i = 0; i < layout->numNodes; i++) {
		free(layout->hosts[i].host);
	}
	delete[] layout->hosts;
}
