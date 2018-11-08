#ifndef _EXECVPOUTPUT_HPP
#define _EXECVPOUTPUT_HPP

#include <unistd.h>
#include <sys/wait.h>

#include "FdBuf.hpp"

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
