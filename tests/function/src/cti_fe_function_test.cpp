#include "cti_fe_function_test.hpp"

void assert_true(bool condition, std::string error) {
    if (!condition) {
        std::cerr << error << std::endl;
        exit(-1);
    }
}

// take a vector of strings and prepend the system specific arguements to it
std::vector<std::string> createSystemArgv(int argc, char* mainArgv[], const std::vector<std::string>& appArgv) {
    auto fullArgv = std::vector<std::string>{};
    
    // split system specific args by whitespace and insert into fullArgv
    if (argc > 1) {
        std::istringstream iss(mainArgv[1]);
        std::copy(std::istream_iterator<std::string>(iss), std::istream_iterator<std::string>(), std::back_inserter(fullArgv));
    }

    // append passed in argv
    std::copy(appArgv.begin(), appArgv.end(), std::back_inserter(fullArgv));

    for (const auto &str : fullArgv) {
        std::cout << str << ", ";
    }
    std::cout << std::endl;

    return fullArgv;
}

// take a vector of strings, copy their c_str() pointers to a new vector,
// and add a nullptr at the end. the return value can then be used in
// ctiLaunchApp and similar via "return_value.data()"
std::vector<const char*> cstrVector(const std::vector<std::string> &v) {
    std::vector<const char*> r;

    for (const auto &str : v) {
        r.push_back(str.c_str());
    }

    r.push_back(nullptr);

    return r;
}

// Find my external IP
std::string getExternalAddress()
{
    // Get information structs about all network interfaces
    struct ifaddrs *ifaddr = nullptr;
    if (getifaddrs(&ifaddr) < 0) {
        throw std::runtime_error(strerror(errno));
    }

    // Find the first IP address that isn't localhost
    for (struct ifaddrs* ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL) {
            continue;
        }

        // Limit to IPv4 and IPv6
        auto const family = ifa->ifa_addr->sa_family;
        if ((family == AF_INET) || (family == AF_INET6)) {

            // Skip if loopback
            if (ifa->ifa_flags & IFF_LOOPBACK) {
                continue;
            }

            // Get hostname for interface
            char address[NI_MAXHOST];
            auto const sockaddr_size = (family == AF_INET)
                ? sizeof(struct sockaddr_in)
                : sizeof(struct sockaddr_in6);

            if (auto const rc = getnameinfo(ifa->ifa_addr, sockaddr_size,
                address, NI_MAXHOST, nullptr, 0, NI_NUMERICHOST)) {

                // Clean up ifaddr
                freeifaddrs(ifaddr);

                throw std::runtime_error(strerror(errno));
            }

            // Clean up ifaddr
            freeifaddrs(ifaddr);

            return std::string{address};
        }
    }

    // Clean up ifaddr
    freeifaddrs(ifaddr);

    throw std::runtime_error("failed to find any external address");
}

int bindAny(std::string const& address)
{
    // setup hints
    struct addrinfo hints;
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_NUMERICSERV;

    // uses external_ip in order to bind socket to external IP and not localhost
    // if NULL is used this will ALWAYS give localhost which is not non-wbox compatible
    struct addrinfo *raw_listener = nullptr;
    if (auto const rc = getaddrinfo(address.c_str(), "0", &hints, &raw_listener)) {
        throw std::runtime_error(gai_strerror(rc));
    }
    auto listener = std::unique_ptr<struct addrinfo, decltype(&freeaddrinfo)>(std::move(raw_listener), freeaddrinfo);
    raw_listener = nullptr;

    // Create the socket
    auto const socketFd = socket(listener->ai_family, listener->ai_socktype, listener->ai_protocol);
    if (socketFd < 0) {
        throw std::runtime_error(strerror(errno));
    }

    // Bind the socket
    if (bind(socketFd, listener->ai_addr, listener->ai_addrlen) < 0) {
        throw std::runtime_error(strerror(errno));
    }

    return socketFd;
}

void testSocketDaemon(cti_session_id_t sessionId, char const* daemonPath, std::vector<char const*> extra_argv, std::string const& expecting, int times) {
    // Wait for any previous cleanups to finish (see PE-26018)
    sleep(5);

    std::cout << "Getting address and starting to listen...\n";
    // Get address accessible from compute node
    auto const address = getExternalAddress();

    // build 'server' socket
    auto const test_socket = bindAny(address);

    // Begin listening on socket
    assert_true(listen(test_socket, 1) == 0, "Failed to listen on test_socket socket");

    // get my sockets info
    struct sockaddr_in sa;
    socklen_t sa_len = sizeof(sa);
    memset(&sa, 0, sizeof(sa));
    assert_true(getsockname(test_socket, (struct sockaddr*) &sa, &sa_len) == 0, "getsockname");

    // build required parameters for launching external app
    {
        std::cout << "Launching app...\n";
        // create manifest and args
        auto const manifestId = cti_createManifest(sessionId);
        assert_true(cti_manifestIsValid(manifestId), cti_error_str());
        std::vector<char const*> v_argv = {address.c_str(), std::to_string(ntohs(sa.sin_port)).c_str()};
        v_argv.insert(v_argv.end(), extra_argv.begin(), extra_argv.end());
        v_argv.push_back(nullptr);

        for (const auto &arg : v_argv) {
            if (arg != nullptr) {
                std::cout << arg << std::endl;
            }
        }

        // launch app
        assert_true(cti_execToolDaemon(manifestId, daemonPath, v_argv.data(), nullptr) == SUCCESS, cti_error_str());
        std::cout << "App launched. Net info: " << address << " " << std::to_string(ntohs(sa.sin_port)) << "\n";
    }

    // accept recently launched applications connection
    std::cout << "Waiting for communication from app...\n";
    int app_socket;
    struct sockaddr_in wa;
    socklen_t wa_len = sizeof(wa);

    for (int i = 0; i < times; ++i) {
        app_socket = accept(test_socket, (struct sockaddr*) &wa, &wa_len);
        assert_true(app_socket >= 0, "accept");

        // read data returned from app
        std::cout << "Reading data...\n";
        char buffer[16] = {0};
        int length = read(app_socket, buffer, 16);
        assert_true(length < 16, "length >= 16");
        buffer[length] = '\0';

        // check for correctness
        std::cout << "Checking for correctness...\n";
        assert_true(strcmp(buffer, expecting.c_str()) == 0, "strcmp");
    }

    // close socket
    std::cout << "Closing socket...\n";
    close(test_socket);

    std::cout << "Done!\n";
}
