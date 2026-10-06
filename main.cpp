#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include <CLI/CLI.hpp>

#include "shmscope/ui/application.hpp"
#include "shmscope/version.hpp"

namespace {
    constexpr std::string_view HELP_FOOTER = R"(
Keys:
  arrows, PgUp/PgDn, Home/End  move
  space                        freeze or resume updates
  f                            follow the writer
  i                            switch fields / inspector
  /                            command bar, Tab completes
  q, Esc                       back

Examples:
  shmscope                                      open the launcher
  shmscope /shmscope-demo                       watch a segment
  shmscope -l examples/ring.ksy /shmscope-demo  decode it with a layout
  shmscope --hz 60 /shmscope-demo               refresh 60 times a second

Docs: https://shmscope.ayushacharjya.com/
)";

    class HelpFormatter : public CLI::Formatter {
    public:
        std::string make_help(const CLI::App* app, std::string name,
                              CLI::AppFormatMode mode) const override {
            return CLI::Formatter::make_help(app, std::move(name), mode) +
                   std::string(HELP_FOOTER);
        }
    };
}  // namespace

int main(const int argc, char** argv) {
    CLI::App cli{"Live terminal viewer for POSIX shared memory.", "shmscope"};
    cli.set_version_flag("-v,--version", SHMSCOPE_VERSION);
    auto formatter = std::make_shared<HelpFormatter>();
    formatter->column_width(26);
    cli.formatter(formatter);

    shmscope::Options options;
    cli.add_option("name", options.name,
                   "Shared memory object to open, e.g. /shmscope-demo.\n"
                   "Leave it out to pick from recent segments.")
        ->type_name("NAME");
    cli.add_option("-l,--layout", options.layout,
                   "Layout file (.ksy, .yaml or .json) that decodes the\n"
                   "bytes into named fields.")
        ->type_name("FILE")
        ->check(CLI::Validator(CLI::ExistingFile).description(""));
    cli.add_option("--hz", options.hz, "Refreshes per second, 1 to 60.")
        ->type_name("RATE")
        ->check(CLI::Validator(CLI::Range(1, 60)).description(""))
        ->capture_default_str();

    CLI11_PARSE(cli, argc, argv);

    shmscope::Application app(std::move(options));

    return app.run();
}
