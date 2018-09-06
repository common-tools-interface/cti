#include "ArgvDefs.hpp"

// flaw in C++11 relating to static constexpr. can be removed in C++17

constexpr Argv::GNUOption Argv::long_options_done;

using SA = SattachArgv;
constexpr Option    SA::PrependWithTaskLabel, SA::DisplayLayout, SA::RunInPty, SA::QuietOutput,
	SA::VerboseOutput;
constexpr Parameter SA::InputFilter, SA::OutputFilter, SA::ErrorFilter;
constexpr Argv::GNUOption SA::long_options[];

using DA = DaemonArgv;
constexpr Option    DA::Clean, DA::Help, DA::Debug;
constexpr Parameter DA::ApID, DA::Binary, DA::Directory, DA::EnvVariable, DA::InstSeqNum,
	DA::ManifestName, DA::ToolPath, DA::PMIAttribsPath, DA::WLMEnum;
constexpr Argv::GNUOption DA::long_options[];