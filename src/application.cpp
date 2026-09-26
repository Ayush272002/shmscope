#include "shmscope/application.hpp"

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <ostream>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>

#include "shmscope/shm_source.hpp"

namespace shmscope {

    namespace {

        constexpr std::string_view ALTERNATE_SCROLL_ON = "\x1b[?1007h";
        constexpr std::string_view ALTERNATE_SCROLL_OFF = "\x1b[?1007l";

        void send(const std::string_view sequence) {
            std::fwrite(sequence.data(), 1, sequence.size(), stdout);
            std::fflush(stdout);
        }

    }  // namespace

    Application::Application(Options options)
        : options_(std::move(options)),
          terminal_(ftxui::App::Fullscreen()),
          launcher_(
              [this](const std::string& name) {
                  if (auto error = open(name)) {
                      launcher_.setError(std::move(*error));
                  }
              },
              [this] { terminal_.Exit(); }, options_.hz),
          viewer_(options_.hz, [this] { close(); }) {}

    int Application::run() {
        if (options_.name) {
            if (auto error = open(*options_.name)) {
                std::println(stderr, "shmscope: {}", *error);
                return 1;
            }
        }

        const auto root =
            ftxui::Container::Tab({launcher_.component(), viewer_.component()},
                                  &active_) |
            ftxui::CatchEvent([this](const ftxui::Event& event) {
                if (event == ftxui::Event::CtrlC) {
                    terminal_.Exit();
                    return true;
                }

                if (event != ftxui::Event::Custom) return false;
                if (active_ == LAUNCHER)
                    launcher_.tick();
                else
                    viewer_.tick();
                return true;
            });

        std::jthread ticker([this](const std::stop_token& stop) {
            const auto period =
                std::chrono::microseconds(1'000'000 / options_.hz);
            std::mutex mutex;
            std::condition_variable_any wake;
            std::unique_lock lock(mutex);
            auto next = std::chrono::steady_clock::now() + period;
            while (!wake.wait_until(lock, stop, next, [] { return false; })) {
                if (stop.stop_requested()) {
                    break;
                }
                terminal_.PostEvent(ftxui::Event::Custom);
                next += period;

                const auto now = std::chrono::steady_clock::now();
                if (next < now) {
                    next = now + period;
                }
            }
        });

        terminal_.TrackMouse(false);
        terminal_.ForceHandleCtrlC(false);
        send(ALTERNATE_SCROLL_ON);
        terminal_.Loop(root);
        send(ALTERNATE_SCROLL_OFF);
        return 0;
    }

    std::optional<std::string> Application::open(const std::string& name) {
        auto source = ShmSource::open(name);
        if (!source) {
            return std::move(source.error());
        }

        viewer_.attach(std::move(*source));
        active_ = VIEWER;
        return std::nullopt;
    }

    void Application::close() {
        viewer_.detach();
        active_ = LAUNCHER;
    }

}  // namespace shmscope
