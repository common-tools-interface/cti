/*********************************************************************************\
 * cti_execvp.hpp - fork / execvp a program and read its output as an istream
 *
 * Copyright 2014-2020 Hewlett Packard Enterprise Development LP.
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

#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <string.h>

#include <sstream>
#include <streambuf>

namespace cti {

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

        while (true) {
            ssize_t numBytesRead = read(fd, &readCh, 1);
            if (numBytesRead == 0) {
                return EOF;
            } else if (numBytesRead < 0) {
                if (errno == EAGAIN) {
                    continue;
                } else {
                    throw std::runtime_error("read failed: " + std::string{strerror(errno)});
                }
            } else {
                break;
            }
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

class FdPair {
protected:
    enum Ends { ReadEnd = 0, WriteEnd = 1 };
    bool readOpened = false;
    bool writeOpened = false;

    int fds[2];

public:
    static const int stdin = 0;
    static const int stdout = 1;
    static const int stderr = 2;

    FdPair()
        : readOpened{false}
        , writeOpened{false}
        , fds{-1, -1}
    {}

    ~FdPair() {
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

    /* Pipe - create and track closed ends of pipe */
    void pipe(int flags = 0)
    {
        if (readOpened || writeOpened) {
            throw std::runtime_error("read or write pipe already opened");
        }

        if (::pipe2(fds, flags)) {
            throw std::runtime_error("Pipe error");
        }

        readOpened = true;
        writeOpened = true;
    }

    /* SocketPair - create and track socket pair */
    void socketpair(int domain, int type, int protocol)
    {
        if (readOpened || writeOpened) {
            throw std::runtime_error("read or write pipe already opened");
        }

        if (::socketpair(domain, type, protocol, fds)) {
            throw std::runtime_error("socketpair error");
        }

        readOpened = true;
        writeOpened = true;
    }
};

struct Pipe : public FdPair
{
    Pipe(int flags = 0)
        : FdPair{}
    {
        pipe(flags);
    }
};

/* Execvp - fork / execvp a program and read its output as an istream */
// Right now the only constructor is to read output as an istream, but this could
// be extended in the future to accept different types of constructors.
class Execvp {
public:
   enum class stderr : int
       { Ignore = 0
       , Pipe = 1
   };

private:
    Pipe p;
    FdBuf pipeInBuf;
    std::istream pipein;
    pid_t child;

public:
    Execvp(const char *binaryName, char* const* argv, stderr stderr_behavior)
        : pipeInBuf(p.getReadFd())
        , pipein(&pipeInBuf)
        , child(fork())
    {
        if (child < 0) {
            throw std::runtime_error(std::string("fork() for ") + binaryName + " failed!");
        } else if (child == 0) { // child side of fork
            /* prepare the output pipe */
            p.closeRead();
            dup2(p.getWriteFd(), Pipe::stdout);
            auto const stderr_fd = (stderr_behavior == stderr::Ignore)
                ? open("/dev/null", O_WRONLY)
                : p.getWriteFd();
            dup2(stderr_fd, Pipe::stderr);
            p.closeWrite();

            execvp(binaryName, argv);
            _exit(-1);
        }

        /* create istream from output pipe */
        p.closeWrite();
    }

    int getExitStatus() {
        int status = 0;
        while (true) {
            auto const rc = ::waitpid(child, &status, 0);
            if (rc < 0) {
                if (errno == EINTR) {
                    continue;
                } else {
                    throw std::runtime_error("waitpid() on " + std::to_string(child) + " failed!");
                }
            } else {
                return WEXITSTATUS(status);
            }
        }
    }

    std::istream& stream() { return pipein; }

    static int runExitStatus(const char *binaryName, char* const* argv) {
        auto const child = fork();

        if (child < 0) {
            throw std::runtime_error(std::string("fork() for ") + binaryName + " failed!");
        } else if (child == 0) { // child side of fork
            dup2(open("/dev/null", O_RDONLY), STDIN_FILENO);
            dup2(open("/dev/null", O_WRONLY), STDOUT_FILENO);
            dup2(open("/dev/null", O_WRONLY), STDERR_FILENO);

            execvp(binaryName, argv);
            _exit(-1);
        }

        int status = 0;
        if ((waitpid(child, &status, 0) < 0) && (errno != ECHILD)) {
            throw std::runtime_error(
                std::string("waitpid() on ") + std::to_string(child) + " failed!");
        }
        return WEXITSTATUS(status);
    }
};

} /* namespace cti */
