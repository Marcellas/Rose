#include "files/SourceWindowDigest.h"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace rose::files
{
    namespace
    {
        constexpr std::array<std::uint32_t, 64> roundConstants{
            0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
            0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
            0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
            0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
            0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
            0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
            0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
            0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
            0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
            0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
            0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
            0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
            0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
            0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
            0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
            0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
        };

        [[nodiscard]]
        std::uint32_t choose(
            const std::uint32_t x,
            const std::uint32_t y,
            const std::uint32_t z) noexcept
        {
            return (x & y) ^ (~x & z);
        }

        [[nodiscard]]
        std::uint32_t majority(
            const std::uint32_t x,
            const std::uint32_t y,
            const std::uint32_t z) noexcept
        {
            return (x & y) ^ (x & z) ^ (y & z);
        }

        [[nodiscard]]
        std::uint32_t bigSigma0(
            const std::uint32_t value) noexcept
        {
            return std::rotr(value, 2)
                ^ std::rotr(value, 13)
                ^ std::rotr(value, 22);
        }

        [[nodiscard]]
        std::uint32_t bigSigma1(
            const std::uint32_t value) noexcept
        {
            return std::rotr(value, 6)
                ^ std::rotr(value, 11)
                ^ std::rotr(value, 25);
        }

        [[nodiscard]]
        std::uint32_t smallSigma0(
            const std::uint32_t value) noexcept
        {
            return std::rotr(value, 7)
                ^ std::rotr(value, 18)
                ^ (value >> 3u);
        }

        [[nodiscard]]
        std::uint32_t smallSigma1(
            const std::uint32_t value) noexcept
        {
            return std::rotr(value, 17)
                ^ std::rotr(value, 19)
                ^ (value >> 10u);
        }
    } // namespace


    std::string canonicalObservedSourceWindow(
        const std::string_view observedRangeText)
    {
        std::string logical;
        logical.reserve(observedRangeText.size());

        for (std::size_t index = 0; index < observedRangeText.size(); ++index)
        {
            if (
                observedRangeText[index] == '\r'
                && index + 1 < observedRangeText.size()
                && observedRangeText[index + 1] == '\n')
            {
                logical.push_back('\n');
                ++index;
                continue;
            }

            logical.push_back(observedRangeText[index]);
        }

        // readTextFileLines includes the line delimiter following the last
        // selected line when one exists. replace_line_range deliberately excludes
        // that delimiter from its preimage, so remove exactly one trailing LF.
        if (!logical.empty() && logical.back() == '\n')
        {
            logical.pop_back();
        }

        return logical;
    }


        [[nodiscard]]
        std::string rawSha256(
            const std::string_view bytesToHash)
        {
        std::vector<std::uint8_t> bytes;
        bytes.reserve(bytesToHash.size() + 72u);

        for (const char character : bytesToHash)
        {
            bytes.push_back(
                static_cast<std::uint8_t>(
                    static_cast<unsigned char>(character)));
        }

        const std::uint64_t bitLength =
            static_cast<std::uint64_t>(bytesToHash.size()) * 8u;

        bytes.push_back(0x80u);
        while ((bytes.size() % 64u) != 56u)
        {
            bytes.push_back(0u);
        }

        for (int shift = 56; shift >= 0; shift -= 8)
        {
            bytes.push_back(
                static_cast<std::uint8_t>(
                    (bitLength >> static_cast<unsigned int>(shift)) & 0xffu));
        }

        std::array<std::uint32_t, 8> state{
            0x6a09e667u,
            0xbb67ae85u,
            0x3c6ef372u,
            0xa54ff53au,
            0x510e527fu,
            0x9b05688cu,
            0x1f83d9abu,
            0x5be0cd19u
        };

        for (std::size_t offset = 0; offset < bytes.size(); offset += 64u)
        {
            std::array<std::uint32_t, 64> words{};

            for (std::size_t index = 0; index < 16u; ++index)
            {
                const std::size_t base = offset + index * 4u;
                words[index] =
                    (static_cast<std::uint32_t>(bytes[base]) << 24u)
                    | (static_cast<std::uint32_t>(bytes[base + 1u]) << 16u)
                    | (static_cast<std::uint32_t>(bytes[base + 2u]) << 8u)
                    | static_cast<std::uint32_t>(bytes[base + 3u]);
            }

            for (std::size_t index = 16u; index < words.size(); ++index)
            {
                words[index] =
                    smallSigma1(words[index - 2u])
                    + words[index - 7u]
                    + smallSigma0(words[index - 15u])
                    + words[index - 16u];
            }

            std::uint32_t a = state[0];
            std::uint32_t b = state[1];
            std::uint32_t c = state[2];
            std::uint32_t d = state[3];
            std::uint32_t e = state[4];
            std::uint32_t f = state[5];
            std::uint32_t g = state[6];
            std::uint32_t h = state[7];

            for (std::size_t index = 0; index < 64u; ++index)
            {
                const std::uint32_t temporary1 =
                    h
                    + bigSigma1(e)
                    + choose(e, f, g)
                    + roundConstants[index]
                    + words[index];

                const std::uint32_t temporary2 =
                    bigSigma0(a)
                    + majority(a, b, c);

                h = g;
                g = f;
                f = e;
                e = d + temporary1;
                d = c;
                c = b;
                b = a;
                a = temporary1 + temporary2;
            }

            state[0] += a;
            state[1] += b;
            state[2] += c;
            state[3] += d;
            state[4] += e;
            state[5] += f;
            state[6] += g;
            state[7] += h;
        }

        std::ostringstream text;
        text << std::hex << std::setfill('0');

        for (const std::uint32_t word : state)
        {
            text << std::setw(8) << word;
        }

        return text.str();
    }


    std::vector<std::string> sourceLineSha256s(
        const std::string_view logicalSourceWindow)
    {
        std::vector<std::string> digests;

        std::size_t begin{ 0 };
        while (true)
        {
            const std::size_t newline =
                logicalSourceWindow.find('\n', begin);

            const std::size_t end =
                newline == std::string_view::npos
                    ? logicalSourceWindow.size()
                    : newline;

            digests.push_back(
                rawSha256(
                    logicalSourceWindow.substr(
                        begin,
                        end - begin)));

            if (newline == std::string_view::npos)
            {
                break;
            }

            begin = newline + 1;
        }

        return digests;
    }


    std::string sourceWindowSha256FromLineDigests(
        const std::span<const std::string> lineDigests)
    {
        std::string framed{ "rose-source-window-v1\n" };
        framed.reserve(
            framed.size()
            + lineDigests.size() * 65u);

        for (const std::string& digest : lineDigests)
        {
            if (!isSourceWindowSha256(digest))
            {
                return {};
            }

            framed += digest;
            framed.push_back('\n');
        }

        return rawSha256(framed);
    }


    std::string sourceWindowSha256(
        const std::string_view logicalSourceWindow)
    {
        const std::vector<std::string> lineDigests =
            sourceLineSha256s(logicalSourceWindow);

        return sourceWindowSha256FromLineDigests(
            lineDigests);
    }


    bool isSourceWindowSha256(
        const std::string_view digest) noexcept
    {
        if (digest.size() != 64u)
        {
            return false;
        }

        for (const char character : digest)
        {
            const bool decimal = character >= '0' && character <= '9';
            const bool lowerHex = character >= 'a' && character <= 'f';

            if (!decimal && !lowerHex)
            {
                return false;
            }
        }

        return true;
    }

} // namespace rose::files
