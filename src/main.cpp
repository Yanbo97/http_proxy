#include "Server.h"


void init_logging();

void daemonize() {
    pid_t pid;
    pid = fork();
    if (pid < 0) {
        exit(EXIT_FAILURE);
    }
    if (pid > 0) {
        exit(EXIT_SUCCESS);
    }
    if (setsid() < 0) {
        exit(EXIT_FAILURE);
    }

    // Catch, ignore and handle signals
    signal(SIGCHLD, SIG_IGN);
    signal(SIGHUP, SIG_IGN);

    pid = fork();
    if (pid < 0) {
        exit(EXIT_FAILURE);
    }
    if (pid > 0) {
        exit(EXIT_SUCCESS);
    }

    umask(0);
    chdir("/");
    std::cout << "running on pid: " << pid << std::endl;

    // Close file descriptors
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

// Open /dev/null and duplicate it for standard input, output, and error
    int devNull = open("/dev/null", O_RDWR);
    dup2(devNull, STDIN_FILENO);
    dup2(devNull, STDOUT_FILENO);
    dup2(devNull, STDERR_FILENO);
}

int main() {
    daemonize();
    init_logging();
    auto const address = asio::ip::make_address("0.0.0.0");
    short PORT = 12345;
    int const threads = 10;
    asio::io_context io_context{threads};
    std::make_shared<Server>(
            io_context,
            tcp::endpoint(address, PORT))->run();

    asio::signal_set signals(io_context, SIGINT, SIGTERM);
    signals.async_wait(
            [&](beast::error_code const &, int) {
                boost::log::core::get()->flush();
                io_context.stop();
            });
    std::vector<std::thread> v;

    v.reserve(threads - 1);
    for (auto i = threads - 1; i > 0; --i)
        v.emplace_back(
                [&io_context] {
                    io_context.run();
                });
    io_context.run();
    for (auto &t: v)
        t.join();
    return 0;
}


void init_logging() {
    boost::log::add_file_log(
            boost::log::keywords::file_name = "var/log/erss/proxy_%N.log",
            boost::log::keywords::rotation_size = 10 * 1024 * 1024,
            boost::log::keywords::time_based_rotation = boost::log::sinks::file::rotation_at_time_point(0, 0, 0),
            boost::log::keywords::format = "[%Severity%]: %Message% [%TimeStamp%]"
    );
    boost::log::add_common_attributes();
}