#include <utility>

#include <CLI/CLI.hpp>

#include "shmscope/application.hpp"
#include "shmscope/version.hpp"

int main(const int argc, char** argv) {
    CLI::App cli{"Live terminal viewer for POSIX shared memory.", "shmscope"};
    cli.set_version_flag("-v,--version", SHMSCOPE_VERSION);

    shmscope::Options options;
    cli.add_option("name", options.name,
                   "shared memory object to open, e.g. /something.shm");
    cli.add_option("--hz", options.hz, "refresh rate")
        ->check(CLI::Range(1, 60))
        ->capture_default_str();

    CLI11_PARSE(cli, argc, argv);

    shmscope::Application app(std::move(options));

    return app.run();
}
