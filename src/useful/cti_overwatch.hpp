#pragma once

#include "useful/MsgQueue.hpp"

enum OverwatchMsgType : long {
	Any = 0,
	AppRegister,
	UtilityRegister,
	AppDeregister,
	Shutdown
};
struct OverwatchData {
	pid_t appPid;
	pid_t utilPid;
};

using OverwatchQueue = MsgQueue<OverwatchMsgType, OverwatchData>;