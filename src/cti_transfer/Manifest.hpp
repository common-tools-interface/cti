#pragma once

#include <memory>

// forward declare Session
class Session;

class Manifest {
	const std::weak_ptr<Session> session;
public:
	Manifest(std::shared_ptr<Session> session_) : session(session_) {}
};

