// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "pch.h"
#include "RemoteIconCache.h"
#include "FileUtils.h"

#include <til/hash.h>
#include <winrt/Windows.Web.Http.h>
#include <winrt/Windows.Web.Http.Headers.h>

using namespace winrt::Windows::Web::Http;
using namespace winrt::Windows::Storage::Streams;

namespace
{
    // Nothing legitimate here is large. A favicon is a couple of KB; the cap is
    // what stops a hostile or misconfigured URL from writing until the disk is
    // full, since we are fetching a URL somebody else's package chose.
    constexpr uint32_t MaxIconBytes = 1u * 1024u * 1024u;

    // Only extensions an image loader will actually accept. An unrecognised one
    // becomes .png rather than being trusted: the extension comes from a remote
    // URL, and it decides what the shell will try to do with the file.
    std::wstring_view SafeExtension(const std::wstring_view url)
    {
        static constexpr std::array known{
            std::wstring_view{ L".png" },
            std::wstring_view{ L".ico" },
            std::wstring_view{ L".jpg" },
            std::wstring_view{ L".jpeg" },
            std::wstring_view{ L".gif" },
            std::wstring_view{ L".bmp" },
        };

        // Stop at a query string: ".../icon.png?v=2" is still a png.
        auto path{ url.substr(0, url.find_first_of(L"?#")) };
        const auto dot{ path.find_last_of(L'.') };
        if (dot != std::wstring_view::npos)
        {
            const auto ext{ path.substr(dot) };
            for (const auto& k : known)
            {
                if (til::equals_insensitive_ascii(ext, k))
                {
                    return k;
                }
            }
        }
        return L".png";
    }

    std::filesystem::path CacheDirectory()
    {
        return ::winrt::Microsoft::Terminal::Settings::Model::GetBaseSettingsPath() / L"RemoteIcons";
    }

    std::filesystem::path CachePathFor(const std::wstring_view url)
    {
        // Hashed rather than derived from the URL text: URLs contain characters
        // a path cannot, and two different URLs can share a filename.
        return CacheDirectory() / fmt::format(FMT_COMPILE(L"{:016x}{}"), til::hash(url), SafeExtension(url));
    }

    // URLs currently being fetched. Settings resolution runs over every profile
    // and can run repeatedly, so without this a slow request would be started
    // again on each pass.
    til::shared_mutex<std::unordered_set<std::wstring>>& InFlight() noexcept
    {
        static til::shared_mutex<std::unordered_set<std::wstring>> inFlight;
        return inFlight;
    }

    winrt::fire_and_forget DownloadAsync(std::wstring url)
    try
    {
        co_await winrt::resume_background();

        const auto release = wil::scope_exit([&]() {
            InFlight().lock()->erase(url);
        });

        const auto destination{ CachePathFor(url) };

        HttpClient client;
        // Some CDNs serve a different (or no) response without one.
        client.DefaultRequestHeaders().UserAgent().TryParseAdd(L"WindowsTerminal");

        const auto response{ co_await client.GetAsync(winrt::Windows::Foundation::Uri{ url }, HttpCompletionOption::ResponseHeadersRead) };
        if (!response.IsSuccessStatusCode())
        {
            co_return;
        }

        // Check the advertised length before reading, so an oversized body is
        // refused rather than buffered.
        if (const auto length{ response.Content().Headers().ContentLength() })
        {
            if (length.Value() > MaxIconBytes)
            {
                co_return;
            }
        }

        const auto buffer{ co_await response.Content().ReadAsBufferAsync() };
        if (!buffer || buffer.Length() == 0 || buffer.Length() > MaxIconBytes)
        {
            co_return;
        }

        std::error_code ec;
        std::filesystem::create_directories(CacheDirectory(), ec);
        if (ec)
        {
            co_return;
        }

        // Temp file then rename, so a reader never sees a half-written icon and
        // two Terminals fetching the same URL cannot collide.
        const auto temp{ std::filesystem::path{ destination }.concat(fmt::format(FMT_COMPILE(L".{}.tmp"), GetCurrentProcessId())) };
        {
            const auto reader{ DataReader::FromBuffer(buffer) };
            std::vector<uint8_t> bytes(buffer.Length());
            reader.ReadBytes(bytes);

            wil::unique_hfile file{ CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr) };
            THROW_LAST_ERROR_IF(!file);
            DWORD written{};
            THROW_IF_WIN32_BOOL_FALSE(WriteFile(file.get(), bytes.data(), gsl::narrow_cast<DWORD>(bytes.size()), &written, nullptr));
        }
        THROW_IF_WIN32_BOOL_FALSE(MoveFileExW(temp.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING));
    }
    CATCH_LOG()
}

namespace winrt::Microsoft::Terminal::Settings::Model
{
    std::optional<std::wstring> TryGetCachedRemoteIcon(const std::wstring_view url)
    try
    {
        auto path{ CachePathFor(url) };
        std::error_code ec;
        if (std::filesystem::exists(path, ec) && !ec)
        {
            return path.wstring();
        }
        return std::nullopt;
    }
    catch (...)
    {
        LOG_CAUGHT_EXCEPTION();
        return std::nullopt;
    }

    void RequestRemoteIcon(const std::wstring_view url)
    try
    {
        std::wstring key{ url };
        {
            auto inFlight{ InFlight().lock() };
            if (!inFlight->emplace(key).second)
            {
                return;
            }
        }
        DownloadAsync(std::move(key));
    }
    CATCH_LOG()
}
