#include "shmscope/core/diff.hpp"

#include <cstddef>
#include <cstdint>

#include "shmscope/core/source.hpp"

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "core/diff.cpp"
#include <hwy/foreach_target.h>
#include <hwy/highway.h>

HWY_BEFORE_NAMESPACE();

namespace shmscope::HWY_NAMESPACE {

    void copyDiffImpl(const std::uint8_t* HWY_RESTRICT mapping,
                      std::uint8_t* HWY_RESTRICT current,
                      const std::uint8_t* HWY_RESTRICT previous,
                      std::uint8_t* HWY_RESTRICT heat, std::size_t size) {
        const hwy::HWY_NAMESPACE::ScalableTag<std::uint8_t> d;
        const std::size_t lanes = hwy::HWY_NAMESPACE::Lanes(d);
        const auto hot = hwy::HWY_NAMESPACE::Set(d, HEAT_MAX);
        const auto one = hwy::HWY_NAMESPACE::Set(d, 1);

        const auto step = [&](std::size_t at) HWY_ATTR {
            const auto now = hwy::HWY_NAMESPACE::LoadU(d, mapping + at);
            const auto before = hwy::HWY_NAMESPACE::LoadU(d, previous + at);
            const auto decayed = hwy::HWY_NAMESPACE::SaturatedSub(
                hwy::HWY_NAMESPACE::LoadU(d, heat + at), one);
            hwy::HWY_NAMESPACE::StoreU(now, d, current + at);
            hwy::HWY_NAMESPACE::StoreU(
                hwy::HWY_NAMESPACE::IfThenElse(
                    hwy::HWY_NAMESPACE::Eq(now, before), decayed, hot),
                d, heat + at);
        };

        std::size_t i = 0;
        for (; i + (4 * lanes) <= size; i += 4 * lanes) {
            step(i);
            step(i + lanes);
            step(i + (2 * lanes));
            step(i + (3 * lanes));
        }

        for (; i + lanes <= size; i += lanes) {
            step(i);
        }

        for (; i < size; ++i) {
            current[i] = mapping[i];
            const auto decayed =
                static_cast<std::uint8_t>(heat[i] - (heat[i] > 0 ? 1 : 0));
            heat[i] = current[i] != previous[i] ? HEAT_MAX : decayed;
        }
    }

}  // namespace shmscope::HWY_NAMESPACE

HWY_AFTER_NAMESPACE();

#if HWY_ONCE

namespace shmscope {

    HWY_EXPORT(copyDiffImpl);

    void copyDiff(const std::byte* mapping, std::byte* current,
                  const std::byte* previous, std::uint8_t* heat,
                  std::size_t size) noexcept {
        HWY_DYNAMIC_DISPATCH(copyDiffImpl)
        (reinterpret_cast<const std::uint8_t*>(mapping),
         reinterpret_cast<std::uint8_t*>(current),
         reinterpret_cast<const std::uint8_t*>(previous), heat, size);
    }
}  // namespace shmscope

#endif
