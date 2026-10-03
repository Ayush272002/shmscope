#include "shmscope/core/fault_guard.hpp"

#include <atomic>
#include <csetjmp>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>

#include "shmscope/core/diff.hpp"

namespace shmscope {

    namespace {

        thread_local sigjmp_buf* volatile active = nullptr;
        struct sigaction chained{};

        extern "C" void onFault(const int signal, siginfo_t* info,
                                void* context) {
            if (active != nullptr) siglongjmp(*active, 1);

            if ((chained.sa_flags & SA_SIGINFO) != 0 &&
                chained.sa_sigaction != nullptr) {
                chained.sa_sigaction(signal, info, context);
                return;
            }
            if (chained.sa_handler != SIG_DFL &&
                chained.sa_handler != SIG_IGN) {
                chained.sa_handler(signal);
                return;
            }
            ::sigaction(signal, &chained, nullptr);
            ::raise(signal);
        }

        void install() {
            static std::once_flag once;
            std::call_once(once, [] {
                struct sigaction action{};
                action.sa_sigaction = onFault;
                action.sa_flags = SA_SIGINFO | SA_NODEFER;
                sigemptyset(&action.sa_mask);
                ::sigaction(SIGBUS, &action, &chained);
            });
        }

        template <typename Fn>
        bool guarded(Fn&& fn) noexcept {
            install();

            sigjmp_buf jump;
            if (sigsetjmp(jump, 1) != 0) {
                active = nullptr;
                return false;
            }
            active = &jump;
            std::atomic_signal_fence(std::memory_order_seq_cst);
            fn();
            std::atomic_signal_fence(std::memory_order_seq_cst);
            active = nullptr;
            return true;
        }
    }  // namespace

    bool copyGuarded(std::byte* out, const std::byte* mapping,
                     const std::size_t size) noexcept {
        return guarded([&] { std::memcpy(out, mapping, size); });
    }

    bool copyDiffGuarded(const std::byte* mapping, std::byte* current,
                         const std::byte* previous, std::uint8_t* heat,
                         const std::size_t size) noexcept {
        return guarded(
            [&] { copyDiff(mapping, current, previous, heat, size); });
    }
}  // namespace shmscope
