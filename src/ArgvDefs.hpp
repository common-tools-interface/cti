/******************************************************************************\
 * valgrind4hpc_defs.h - Common definitions.
 *
 * Copyright 2015-2017 Cray Inc.  All Rights Reserved.
 *
 * Unpublished Proprietary Information.
 * This unpublished work is protected to trade secret, copyright and other laws.
 * Except as permitted by contract or express written permission of Cray Inc.,
 * no part of this work or its content may be used, reproduced or disclosed
 * in any form.
 *
 ******************************************************************************/

#ifndef ARGV_DEFS_H_
#define ARGV_DEFS_H_

#include "useful/strong_argv.hpp"

using Option    = Argv::Option;
using Parameter = Argv::Parameter;

struct DaemonArgv : public Argv {
	static constexpr Option Clean { "clean", 'c' };
	static constexpr Option Help  { "help",  'h' };
	static constexpr Option Debug { "debug",  1 };

	static constexpr Parameter ApID           { "apid",      'a' };
	static constexpr Parameter Binary         { "binary",    'b' };
	static constexpr Parameter Directory      { "directory", 'd' };
	static constexpr Parameter EnvVariable    { "env",       'e' };
	static constexpr Parameter InstSeqNum     { "inst",      'i' };
	static constexpr Parameter ManifestName   { "manifest",  'm' };
	static constexpr Parameter ToolPath       { "path",      'p' };
	static constexpr Parameter PMIAttribsPath { "apath",     't' };
	static constexpr Parameter WLMEnum        { "wlm",       'w' };

	static constexpr GNUOption long_options[] = {
		Clean, Help, Debug,
		ApID, Binary, Directory, EnvVariable, InstSeqNum, ManifestName, ToolPath, PMIAttribsPath,
			WLMEnum,
	long_options_done };
};

struct SattachArgv : public Argv {
	static constexpr Option PrependWithTaskLabel { "label",    1 };
	static constexpr Option DisplayLayout        { "layout",   2 };
	static constexpr Option RunInPty             { "pty",      3 };
	static constexpr Option QuietOutput          { "quiet",    4 };
	static constexpr Option VerboseOutput        { "verbose",  5 };

	static constexpr Parameter InputFilter  { "input-filter",  6 };
	static constexpr Parameter OutputFilter { "output-filter", 7 };
	static constexpr Parameter ErrorFilter  { "error-filter",  8 };


	static constexpr GNUOption long_options[] = {
		InputFilter, OutputFilter, ErrorFilter,
		PrependWithTaskLabel, DisplayLayout, RunInPty, QuietOutput, VerboseOutput,
	long_options_done };
};

#endif
