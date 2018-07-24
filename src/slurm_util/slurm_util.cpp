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

#include <execvp_stream.hpp>

#include "slurm_util.h"

// sattach standard options
struct SattachArgv : Argv {
	SattachArgv(ProgramName const& name) : Argv(name) {}
	static constexpr ProgramName SattachArgv0 = { "sattach" };

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


slurmStepLayout_t *_cti_cray_slurm_getLayout(uint32_t job_id, uint32_t step_id) {
	// create dotted argument for sattach
	std::string jobIdDotStepId;
	{ std::stringstream ss;
		ss << job_id << "." << step_id;
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

	// create layout container
	slurmStepLayout_t *layout = new slurmStepLayout_t;

	/* sattach layout format:
	Job step layout:
	  {numPEs} tasks, {numNodes} nodes ({hostname}...)
	
	  Node {nodeNum} ({hostname}), {numPEs} task(s): PE_0 {PE_i }...
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

	// "  {numPEs} tasks, {numNodes} nodes ({hostname}...)"
	if (auto layoutSummary = sattachOutput.optional_getline()) {
		// split the summary line
		std::string numPEs, numNodes;
		std::tie(numPEs, std::ignore, numNodes) =
			split::string<3>(split::removeLeadingWhitespace(layoutSummary.value()));

		// fill out sattach layout
		layout->numPEs = std::stoi(numPEs);
		layout->numNodes = std::stoi(numNodes);
		layout->hosts = new slurmNodeLayout_t[layout->numNodes];
	} else {
		throw std::runtime_error("sattach layout: wrong format: expected summary");
	}

	// seperator line
	sattachOutput.optional_getline();

	// "  Node {nodeNum} ({hostname}), {numPEs} task(s): PE_0 {PE_i }..."
	slurmNodeLayout_t *curNodeLayout = &layout->hosts[0];
	while (auto nodeLayout = sattachOutput.optional_getline()) {
		// split the summary line
		std::string nodeNum, hostname, numPEs, pe_0;
		std::tie(std::ignore, nodeNum, hostname, numPEs, std::ignore, pe_0) =
			split::string<6>(split::removeLeadingWhitespace(nodeLayout.value()));

		// remove parens and comma from hostname
		hostname = hostname.substr(1, hostname.length() - 3);

		// fill out node layout
		curNodeLayout->host = strdup(hostname.c_str());
		curNodeLayout->PEsHere = std::stoi(numPEs);
		curNodeLayout->firstPE = std::stoi(pe_0);

		// next node layout
		curNodeLayout++;
	}

	return layout;
}

void _cti_cray_slurm_freeLayout(slurmStepLayout_t *layout) {
	for (int i = 0; i < layout->numNodes; i++) {
		free(layout->hosts[i].host);
	}
	delete layout->hosts;
}