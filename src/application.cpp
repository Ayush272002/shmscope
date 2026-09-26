#include "shmscope/application.hpp"

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <stop_token>
#include <thread>
#include <utility>

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>

#include "shmscope/shm_source.hpp"

namespace shmscope {

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
                if (event != ftxui::Event::Custom) {
                    return false;
                }
                viewer_.tick();
                return true;
            });

        std::jthread ticker([this](const std::stop_token& stop) {
            constexpr auto PERIOD =
                std::chrono::microseconds(1'000'000 / REFRESH_HZ);
            std::mutex mutex;
            std::condition_variable_any wake;
            std::unique_lock lock(mutex);
            auto next = std::chrono::steady_clock::now() + PERIOD;
            while (!wake.wait_until(lock, stop, next, [] { return false; })) {
                if (stop.stop_requested()) {
                    break;
                }
                terminal_.PostEvent(ftxui::Event::Custom);
                next += PERIOD;
            }
        });

        terminal_.Loop(root);
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
