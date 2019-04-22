/******************************************************************************\
 * cti_argv_defs.h - A header file to define the strong argv interface]
 *
 * Copyright 2018-2019 Cray Inc.    All Rights Reserved.
 *
 * Unpublished Proprietary Information.
 * This unpublished work is protected to trade secret, copyright and other laws.
 * Except as permitted by contract or express written permission of Cray Inc.,
 * no part of this work or its content may be used, reproduced or disclosed
 * in any form.
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
        Clean, Help, Debug,
        ApID, Binary, Directory, EnvVariable, InstSeqNum, ManifestName, ToolPath,
            PMIAttribsPath, LdLibraryPath, WLMEnum,
    long_options_done };
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
        InputFilter, OutputFilter, ErrorFilter,
        PrependWithTaskLabel, DisplayLayout, RunInPty, QuietOutput, VerboseOutput,
    long_options_done };
};

// XXX: flaw in C++11 relating to static constexpr. can be removed in C++17
#ifdef INSIDE_WORKAROUND_OBJ

constexpr cti::Argv::GNUOption  cti::Argv::long_options_done;

constexpr cti::Argv::Option     SattachArgv::PrependWithTaskLabel,
                                SattachArgv::DisplayLayout,
                                SattachArgv::RunInPty,
                                SattachArgv::QuietOutput,
                                SattachArgv::VerboseOutput;
constexpr cti::Argv::Parameter  SattachArgv::InputFilter,
                                SattachArgv::OutputFilter,
                                SattachArgv::ErrorFilter;
constexpr cti::Argv::GNUOption  SattachArgv::long_options[];

constexpr cti::Argv::Option     DaemonArgv::Clean,
                                DaemonArgv::Help,
                                DaemonArgv::Debug;
constexpr cti::Argv::Parameter  DaemonArgv::ApID,
                                DaemonArgv::Binary,
                                DaemonArgv::Directory,
                                DaemonArgv::EnvVariable,
                                DaemonArgv::InstSeqNum,
                                DaemonArgv::ManifestName,
                                DaemonArgv::ToolPath,
                                DaemonArgv::PMIAttribsPath,
                                DaemonArgv::LdLibraryPath,
                                DaemonArgv::WLMEnum;

constexpr cti::Argv::GNUOption  DaemonArgv::long_options[];

#endif /* INSIDE_WORKAROUND_OBJ */
