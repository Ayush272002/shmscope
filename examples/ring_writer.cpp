#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <print>
#include <random>
#include <span>
#include <string>
#include <thread>

#include "ring.hpp"

namespace {

    volatile std::sig_atomic_t running = 1;

    extern "C" void stop(int) { running = 0; }

    std::uint64_t nowNanos() {
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::system_clock::now().time_since_epoch())
                .count());
    }

    int fail(const char* what, const std::string& name) {
        std::println(stderr, "ring_writer: {}: {}: {}", name, what,
                     std::strerror(errno));
        return 1;
    }

}  // namespace

int main(const int argc, char** argv) {
    std::string name = argc > 1 ? argv[1] : "/shmscope-demo";
    if (!name.starts_with('/')) name.insert(name.begin(), '/');
    const int rate = argc > 2 ? std::max(1, std::atoi(argv[2])) : 20;

    std::signal(SIGINT, stop);
    std::signal(SIGTERM, stop);

    ::shm_unlink(name.c_str());
    const int fd = ::shm_open(name.c_str(), O_CREAT | O_EXCL | O_RDWR, 0644);
    if (fd < 0) return fail("shm_open", name);

    if (::ftruncate(fd, static_cast<off_t>(ring::BYTES)) != 0) {
        const int result = fail("ftruncate", name);
        ::close(fd);
        ::shm_unlink(name.c_str());
        return result;
    }

    void* base =
        ::mmap(nullptr, ring::BYTES, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (base == MAP_FAILED) {
        const int result = fail("mmap", name);
        ::close(fd);
        ::shm_unlink(name.c_str());
        return result;
    }
    const std::span mapping(static_cast<std::byte*>(base), ring::BYTES);

    ring::writeHeader(mapping, "demo-ticker", nowNanos(), ::getpid());

    std::println("writing {} records a second to {}", rate, name);
    std::println("view it with: shmscope --layout examples/ring.ksy {}", name);
    std::println("Ctrl-C stops the writer and removes the segment");

    std::mt19937_64 random{std::random_device{}()};
    std::uniform_int_distribution<std::int64_t> step(-50, 50);
    std::uniform_int_distribution<std::int64_t> size(1, 500);
    std::bernoulli_distribution coin;

    std::int64_t price = 100 * ring::PRICE_SCALE;
    const auto period = std::chrono::microseconds(1'000'000 / rate);

    for (std::uint64_t sequence = 0; running != 0; ++sequence) {
        price = std::max<std::int64_t>(ring::PRICE_SCALE, price + step(random));
        const auto now = nowNanos();

        if (coin(random)) {
            ring::writeRecord(
                mapping, sequence, now,
                ring::Trade{
                    .price = price,
                    .quantity = size(random),
                    .side = coin(random) ? ring::Side::BUY : ring::Side::SELL});
        } else {
            ring::writeRecord(mapping, sequence, now,
                              ring::Quote{.bidPrice = price - 5,
                                          .bidQuantity = size(random),
                                          .askPrice = price + 5,
                                          .askQuantity = size(random)});
        }
        ring::publish(mapping, sequence + 1, now);

        std::this_thread::sleep_for(period);
    }

    ::munmap(base, ring::BYTES);
    ::close(fd);
    ::shm_unlink(name.c_str());
    std::println("\nremoved {}", name);
    return 0;
}
