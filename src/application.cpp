#include "shmscope/application.hpp"

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
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

        void send(std::string_view sequence) {
            std::fwrite(sequence.data(), 1, sequence.size(), stdout);
            std::fflush(stdout);
        }

    }  // namespace

    Application::Application()
        : terminal_(ftxui::App::Fullscreen()),
          launcher_([this](const std::string& name) { open(name); },
                    [this] { terminal_.Exit(); }),
          viewer_(REFRESH_HZ, [this] { close(); }) {}

    int Application::run() {
        auto root =
            ftxui::Container::Tab({launcher_.component(), viewer_.component()},
                                  &active_) |
            ftxui::CatchEvent([this](const ftxui::Event& event) {
                if (event == ftxui::Event::CtrlC) {
                    terminal_.Exit();
                    return true;
                }
                if (event != ftxui::Event::Custom) {
                    return false;
                }
                viewer_.tick();
                return true;
            });

        std::jthread ticker([this](const std::stop_token& stop) {
            constexpr auto period =
                std::chrono::microseconds(1'000'000 / REFRESH_HZ);
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

    void Application::open(const std::string& name) {
        auto source = ShmSource::open(name);
        if (!source) {
            launcher_.setError(source.error());
            return;
        }

        viewer_.attach(std::move(*source));
        active_ = VIEWER;
    }

    void Application::close() {
        viewer_.detach();
        active_ = LAUNCHER;
    }

}  // namespace shmscope
