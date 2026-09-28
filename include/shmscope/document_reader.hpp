#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <concepts>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "shmscope/document.hpp"

namespace shmscope {

    using ReadResult = std::expected<Node, LoadError>;

    template <typename R>
    concept DocumentReader =
        requires(std::string_view text, std::string_view source) {
            { R::NAME } -> std::convertible_to<std::string_view>;
            { R::EXTENSIONS.size() } -> std::convertible_to<std::size_t>;
            {
                *std::ranges::begin(R::EXTENSIONS)
            } -> std::convertible_to<std::string_view>;
            { R::read(text, source) } -> std::same_as<ReadResult>;
        };

    inline constexpr std::uintmax_t MAX_DOCUMENT_BYTES = 16U * 1024U * 1024U;
    inline constexpr int MAX_DOCUMENT_DEPTH = 128;

    template <DocumentReader... Readers>
    class ReaderSet {
    public:
        [[nodiscard]] static bool supports(std::string_view extension) {
            const std::string lowered = lower(extension);
            return (handles<Readers>(lowered) || ...);
        }

        [[nodiscard]] static std::vector<std::string_view> extensions() {
            std::vector<std::string_view> all;
            (all.insert(all.end(), std::ranges::begin(Readers::EXTENSIONS),
                        std::ranges::end(Readers::EXTENSIONS)),
             ...);

            return all;
        }

        [[nodiscard]] static ReadResult read(std::string_view extension,
                                             std::string_view text,
                                             std::string_view source) {
            const std::string lowered = lower(extension);
            std::optional<ReadResult> result;
            const bool handled =
                ((handles<Readers>(lowered) &&
                  (result.emplace(Readers::read(text, source)), true)) ||
                 ...);

            if (!handled) {
                return std::unexpected(LoadError{
                    .message =
                        std::format("no reader for '{}' files",
                                    extension.empty() ? "(none)" : extension),
                    .source = std::string(source)});
            }

            return std::move(*result);
        }

        [[nodiscard]] static ReadResult readFile(
            const std::filesystem::path& file) {
            const std::string source = file.string();

            std::error_code ec;
            const auto size = std::filesystem::file_size(file, ec);
            if (ec) {
                return std::unexpected(
                    LoadError{.message = ec.message(), .source = source});
            }

            if (size > MAX_DOCUMENT_BYTES) {
                return std::unexpected(LoadError{
                    .message = std::format("file is {} bytes, the limit is {}",
                                           size, MAX_DOCUMENT_BYTES),
                    .source = source});
            }

            std::ifstream in(file, std::ios::binary);
            if (!in) {
                return std::unexpected(
                    LoadError{.message = "cannot open file", .source = source});
            }

            const std::string text{std::istreambuf_iterator<char>(in),
                                   std::istreambuf_iterator<char>()};

            return read(file.extension().string(), text, source);
        }

    private:
        static std::string lower(std::string_view text) {
            std::string out(text);
            std::ranges::transform(out, out.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });

            return out;
        }

        template <typename R>
        static bool handles(std::string_view extension) {
            return std::ranges::find(R::EXTENSIONS, extension) !=
                   std::ranges::end(R::EXTENSIONS);
        }
    };
}  // namespace shmscope
