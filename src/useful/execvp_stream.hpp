#pragma once


// useful: file descriptors to streambuf


#include <sstream>
#include <streambuf>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>

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



// useful: RAII pipes



#include <unistd.h>
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

	Pipe() {
		if (pipe(fds)) {
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


// useful: strongly typed argv array


#include <vector>
#include <string.h>
class Argv {
	std::vector<char*> argv;

	void add_string(const char* str) {
		argv.insert(argv.end() - 1, strdup(str));
	}

public:
	/* don't hide ProgramName to allow custom names if desired */
	struct ProgramName {
		const char* value;
	};
protected:
	/* hide argument types to nudge toward typechecking and good naming */
	struct Option {
		const char* flag;
	};

	struct Parameter {
		const char* flag;
	};

	struct Argument {};

public:
	Argv(ProgramName const& name) {
		argv.push_back(strdup(name.value));
		argv.push_back(NULL);
	}

	~Argv() {
		for (char* str : argv) {
			if (str) { free(str); }
		}
	}

	Argv(Argv&& moved) : argv(std::move(moved.argv)) {}

	char* const *get() const {
		return argv.data();
	}

	void add(ProgramName const& name) {
		add_string(name.value);
	}

	void add(Option const& opt) {
		add_string(opt.flag);
	}

	void add(Parameter const& param, std::string const& value) {
		add_string(param.flag);
		add_string(value.c_str());
	}

	void add(Argument const& arg, std::string const& value) {
		add_string(value.c_str());
	}
};


// useful: execvp output as optional getline


#include "optional.hpp"
#include <unistd.h>
#include <sys/wait.h>
class ExecvpOutput {

	class Line : std::string {
		friend std::istream& operator>>(std::istream& is, Line& line) {
			return std::getline(is, line);
		}
	};

	using MaybeString = nonstd::optional<std::string>;

	static MaybeString optional_getline(std::istream &is, char delim = '\n') {
		std::string result;

		if (!std::getline(is, result, delim)) {
			return nonstd::nullopt;
		}

		return MaybeString(result);
	}

	Pipe p;
	FdBuf pipeInBuf;
	std::istream pipein;
	pid_t child;
public:
	ExecvpOutput(const char *binaryName, Argv const& argv) :
		pipeInBuf(p.getReadFd()), pipein(&pipeInBuf) {

		if ((child = fork()) == 0) { // child side of fork
			/* prepare the output pipe */
			p.closeRead();
			dup2(p.getWriteFd(), Pipe::stdout);

			execvp(binaryName, argv.get());
		}

		/* create istream from output pipe */
		p.closeWrite();
	}

	int getExitStatus() {
		int status;
		waitpid(child, &status, 0);
		return WEXITSTATUS(status);
	}

	MaybeString optional_getline() { return optional_getline(pipein); }
};


// useful: split string into N-tuple


#include <tuple>
#include <utility>
namespace split {
	std::string removeLeadingWhitespace(const std::string& str, const std::string& whitespace = " \t") {
		const auto startPos = str.find_first_not_of(whitespace);
		if (startPos == std::string::npos) return "";
		const auto endPos = str.find_last_not_of(whitespace);
		return str.substr(startPos, endPos - startPos + 1);
	}

	namespace { /* iteration tuple helpers */
		// C++14 could use std::index_sequence...
		template<std::size_t I = 0, typename FuncT, typename... Tp>
		inline typename std::enable_if<I == sizeof...(Tp), void>::type
		for_each(std::tuple<Tp...> &, FuncT) {}

		template<std::size_t I = 0, typename FuncT, typename... Tp>
		inline typename std::enable_if<I < sizeof...(Tp), void>::type
		for_each(std::tuple<Tp...>& t, FuncT f) {
			f(std::get<I>(t));
			for_each<I + 1, FuncT, Tp...>(t, f);
		}
	};

	namespace { /* type-generating tuple helpers */
		template<typename, typename>
		struct append_to_type_seq { };

		template<typename T, typename... Ts, template<typename...> class TT>
		struct append_to_type_seq<T, TT<Ts...>> {
			using type = TT<Ts..., T>;
		};

		template<typename T, unsigned int N, template<typename...> class TT>
		struct repeat {
			using type = typename
				append_to_type_seq<
					T,
					typename repeat<T, N-1, TT>::type
					>::type;
		};

		template<typename T, template<typename...> class TT>
		struct repeat<T, 0, TT> {
			using type = TT<>;
		};
	};

	template <std::size_t N> 
	using NStringTuple = typename repeat<std::string, N, std::tuple>::type;

	template <std::size_t N>
	NStringTuple<N> string(std::string const& line, char delim = ' ') {
		NStringTuple<N> tup;

		std::stringstream linestream(line);

		for_each(tup, [&](std::string& str_target){
			std::getline(linestream, str_target, delim);
		});

		return tup;
	}
};
