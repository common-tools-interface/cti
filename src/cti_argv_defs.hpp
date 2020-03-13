/******************************************************************************\
 * cti_argv_defs.h - A header file to define the strong argv interface
 *
 * Copyright 2018-2019 Cray Inc. All Rights Reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 ******************************************************************************/
#pragma once

// Strong argv defs below
#include "useful/cti_argv.hpp"

struct DaemonArgv : public cti::Argv {
    using Option    = cti::Argv::Option;
    using Parameter = cti::Argv::Parameter;

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
    static constexpr Parameter LdLibraryPath  { "ldlibpath", 'l' };
    static constexpr Parameter WLMEnum        { "wlm",       'w' };

    static constexpr GNUOption long_options[] = {
        Clean,
        Help,
        Debug,
        ApID,
        Binary,
        Directory,
        EnvVariable,
        InstSeqNum,
        ManifestName,
        ToolPath,
        PMIAttribsPath,
        LdLibraryPath,
        WLMEnum,
        long_options_done
    };
};

struct SattachArgv : public cti::Argv {
    using Option    = cti::Argv::Option;
    using Parameter = cti::Argv::Parameter;

    static constexpr Option PrependWithTaskLabel { "label",    1 };
    static constexpr Option DisplayLayout        { "layout",   2 };
    static constexpr Option RunInPty             { "pty",      3 };
    static constexpr Option QuietOutput          { "quiet",    4 };
    static constexpr Option VerboseOutput        { "verbose",  5 };

    static constexpr Parameter InputFilter  { "input-filter",  6 };
    static constexpr Parameter OutputFilter { "output-filter", 7 };
    static constexpr Parameter ErrorFilter  { "error-filter",  8 };


    static constexpr GNUOption long_options[] = {
        InputFilter,
        OutputFilter,
        ErrorFilter,
        PrependWithTaskLabel,
        DisplayLayout,
        RunInPty,
        QuietOutput,
        VerboseOutput,
        long_options_done
    };
};

struct CTIFEDaemonArgv : public cti::Argv {
    using Option    = cti::Argv::Option;
    using Parameter = cti::Argv::Parameter;

    static constexpr Option Help  { "help",  'h' };

    static constexpr Parameter ReadFD  { "read",  'r' };
    static constexpr Parameter WriteFD { "write", 'w' };

    static constexpr GNUOption long_options[] = {
        Help,
        ReadFD,
        WriteFD,
        long_options_done
    };
};

struct PALSLauncherArgv : public cti::Argv {
    using Option    = cti::Argv::Option;
    using Parameter = cti::Argv::Parameter;

    static constexpr Parameter NRanks { "nranks", 'n' };
    static constexpr Parameter PPN    { "ppn",    'N' };
    static constexpr Parameter Depth  { "depth",  'd' };
    static constexpr Parameter NodeList { "node-list",  'L' };

    static constexpr GNUOption long_options[] = {
        NRanks, PPN, Depth, NodeList,
        long_options_done
    };
};
