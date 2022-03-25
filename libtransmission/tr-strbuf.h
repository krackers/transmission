// This file Copyright © 2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0), GPLv3 (SPDX: GPL-3.0),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <string_view>

#include <fmt/format.h>

/**
 * A memory buffer which uses a builtin array of N bytes,
 * but falls back to heap allocation when necessary.
 * Useful for building temp strings without heap allocation.
 *
 * `fmt::basic_memory_buffer` is final, so aggregate intead
 * of subclassing ¯\_(ツ)_/¯
 */
template<typename T, size_t N>
class tr_strbuf
{
private:
    fmt::basic_memory_buffer<T, N> buffer_;

public:
    using value_type = T;
    using const_reference = const T&;

    tr_strbuf() = default;

    auto& operator=(tr_strbuf&& other)
    {
        buffer_ = std::move(other.buffer_);
        return *this;
    }

    template<typename ContiguousRange>
    tr_strbuf(ContiguousRange const& in)
    {
        buffer_.append(in);
    }

    [[nodiscard]] constexpr auto begin()
    {
        return buffer_.begin();
    }

    [[nodiscard]] constexpr auto end()
    {
        return buffer_.end();
    }

    [[nodiscard]] constexpr auto begin() const
    {
        return buffer_.begin();
    }

    [[nodiscard]] constexpr auto end() const
    {
        return buffer_.end();
    }

    [[nodiscard]] T& operator[](size_t pos)
    {
        return buffer_[pos];
    }

    [[nodiscard]] constexpr T const& operator[](size_t pos) const
    {
        return buffer_[pos];
    }

    [[nodiscard]] auto size() const
    {
        return buffer_.size();
    }

    [[nodiscard]] bool empty() const
    {
        return size() == 0;
    }

    [[nodiscard]] auto* data()
    {
        return buffer_.data();
    }

    [[nodiscard]] auto const* data() const
    {
        return buffer_.data();
    }

    ///

    auto clear()
    {
        return buffer_.clear();
    }

    auto resize(size_t n)
    {
        return buffer_.resize(n);
    }

    auto push_back(T const& value)
    {
        return buffer_.push_back(value);
    }

    template<typename ContiguousRange>
    auto append(ContiguousRange const& range)
    {
        return buffer_.append(std::data(range), std::data(range) + std::size(range));
    }

    template<typename ContiguousRange>
    auto& operator+=(ContiguousRange const& range)
    {
        append(range);
        return *this;
    }

    template<typename ContiguousRange>
    auto& operator=(ContiguousRange const& range)
    {
        clear();
        append(range);
        return *this;
    }

    template<typename... ContiguousRange>
    void buildPath(ContiguousRange const&... args)
    {
        buffer_.reserve(sizeof...(args) + (std::size(args) + ...));
        ((append(args), push_back('/')), ...);
        resize(size() - 1);
    }

    /**
     * Ensure that the buffer's string is zero-terminated, e.g. for
     * external APIs that require char* strings.
     *
     * Note that the added trailing '\0' does not increment size().
     * This is to ensure that strlen(buf.c_str()) == buf.size().
     */
    void ensure_sz()
    {
        auto const n = size();
        buffer_.try_reserve(n + 1);
        buffer_[n] = '\0';
    }

    auto const* c_str()
    {
        ensure_sz();
        return data();
    }

    [[nodiscard]] constexpr auto sv() const
    {
        return std::string_view{ data(), size() };
    }
};

/**
 * Good for building short-term URLs.
 * The initial size is big enough to avoid heap allocs in most cases,
 * but that also makes it a poor choice for longer-term storage.
 * https://stackoverflow.com/a/417184
 */
using tr_urlbuf = tr_strbuf<char, 2000>;

/**
 * Good for building short-term filenames.
 * The initial size is big enough to avoid heap allocs in most cases,
 * but that also makes it a poor choice for longer-term storage.
 * https://stackoverflow.com/a/65174437
 */
using tr_pathbuf = tr_strbuf<char, 4096>;