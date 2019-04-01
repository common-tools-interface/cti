/*********************************************************************************\
 * ExecvpOutput.hpp - fork / execvp a program and read its output as an istream
 *
 * Copyright 2014-2019 Cray Inc.	All Rights Reserved.
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

#ifndef _EXECVPOUTPUT_HPP
#define _EXECVPOUTPUT_HPP

#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>

#include <sstream>
#include <streambuf>

/* FdBuf - create a streambuf from a file descriptor */
class FdBuf : public std::streambuf {
public:
	FdBuf(int fd_) : fd(fd_) {
		if (fd_ < 0) {
			throw std::invalid_argument("Invalid file descriptor");
		}
	}

	FdBuf() : fd(-1) {}

	FdBuf(FdBuf&& buf) : fd(buf.fd) {}

	virtual ~FdBuf() {
		if (fd >= 0) {
			close(fd);
		}
	}

protected:
	virtual int underflow() {
		if (fd < 0) { throw std::runtime_error("File descriptor not set"); }

		ssize_t numBytesRead = read(fd, &readCh, 1);
		if (numBytesRead == 0) {
			return EOF;
		}

		setg(&readCh, &readCh, &readCh + 1);
		return static_cast<int>(readCh);
	}

	virtual int overflow(int writeCh) {
		if (fd < 0) { throw std::runtime_error("File descriptor not set"); }

		write(fd, &writeCh, 1);
		return writeCh;
	}

private:
	char readCh;
protected:
	const int fd;
};

/* Pipe - create and track closed ends of pipe */
class Pipe {
private:
	enum Ends { ReadEnd = 0, WriteEnd = 1 };
	bool readOpened = false;
	bool writeOpened = false;

	int fds[2];

public:
	static const int stdin = 0;
	static const int stdout = 1;
	static const int stderr = 2;

	Pipe(int flags = 0) {
		if (pipe2(fds, flags)) {
			throw std::runtime_error("Pipe error");
		}

		readOpened = true;
		writeOpened = true;
	}

	~Pipe() {
		if (readOpened) {
			close(fds[ReadEnd]);
		}

		if (writeOpened) {
			close(fds[WriteEnd]);
		}
	}

	void closeRead() {
		if (!readOpened) {
			throw "Already closed read end";
		}

		close(fds[ReadEnd]);
		readOpened = false;
	}

	void closeWrite() {
		if (!writeOpened) {
			throw "Already closed write end";
		}

		close(fds[WriteEnd]);
		writeOpened = false;
	}

	const int getReadFd() { return fds[ReadEnd]; }
	const int getWriteFd() { return fds[WriteEnd]; }
};

/* ExecvpOutput - fork / execvp a program and read its output as an istream */
class ExecvpOutput {

	class Line : std::string {
		friend std::istream& operator>>(std::istream& is, Line& line) {
			return std::getline(is, line);
		}
	};

	Pipe p;
	FdBuf pipeInBuf;
	std::istream pipein;
	pid_t child;
public:
	ExecvpOutput(const char *binaryName, char* const* argv) :
		pipeInBuf(p.getReadFd()), pipein(&pipeInBuf), child(fork()) {

		if (child < 0) {
			throw std::runtime_error(std::string("fork() for ") + binaryName + " failed!");
		} else if (child == 0) { // child side of fork
			/* prepare the output pipe */
			p.closeRead();
			dup2(p.getWriteFd(), Pipe::stdout);

			execvp(binaryName, argv);
			throw std::runtime_error(std::string("execvp() on ") + binaryName + " failed!");
		}

		/* create istream from output pipe */
		p.closeWrite();
	}

	int getExitStatus() {
		int status;
		if (waitpid(child, &status, 0) < 0) {
			throw std::runtime_error(
				std::string("waitpid() on ") + std::to_string(child) + " failed!");
		}
		return WEXITSTATUS(status);
	}

	std::istream& stream() { return pipein; }
};


#endif