// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "pch.h"
#include "ActivityLog.h"

#include <til/throttled_func.h>

#include "WtExeUtils.h"

using namespace std::chrono_literals;

namespace Microsoft::Terminal::ActivityLog
{
    namespace
    {
        // Matches FileUtils.h's UnpackagedSettingsFolderName. Duplicated rather
        // than shared because that header lives in TerminalSettingsModel, which
        // TerminalConnection does not reference -- and a shared writer that only
        // works in some of the projects that spawn processes is no use.
        constexpr std::wstring_view UnpackagedSettingsFolderName{ L"Microsoft\\Windows Terminal\\" };
        constexpr std::wstring_view LogFilename{ L"activity.jsonl" };
        constexpr std::wstring_view RotatedFilename{ L"activity.1.jsonl" };

        struct State
        {
            std::mutex mutex;
            std::string pending;
            std::atomic<bool> enabled{ false };
            std::atomic<uint32_t> maxBytes{ 4 * 1024 * 1024 };
        };

        State& state() noexcept
        {
            static State s;
            return s;
        }

        // Escape for a JSON string. Control characters have to go somewhere, and
        // a command line can legitimately contain quotes and backslashes -- which
        // on Windows it almost always does.
        void appendEscaped(std::string& out, std::string_view value)
        {
            for (const auto ch : value)
            {
                switch (ch)
                {
                case '"':
                    out += "\\\"";
                    break;
                case '\\':
                    out += "\\\\";
                    break;
                case '\n':
                    out += "\\n";
                    break;
                case '\r':
                    out += "\\r";
                    break;
                case '\t':
                    out += "\\t";
                    break;
                default:
                    if (static_cast<unsigned char>(ch) < 0x20)
                    {
                        // \u00XX. fmt rather than a stringstream: this runs on a
                        // launch path and the rest of this codebase uses fmt.
                        out += fmt::format("\\u{:04x}", static_cast<unsigned int>(static_cast<unsigned char>(ch)));
                    }
                    else
                    {
                        out += ch;
                    }
                    break;
                }
            }
        }

        void appendField(std::string& out, std::string_view name, std::wstring_view value)
        {
            if (value.empty())
            {
                return;
            }
            out += ",\"";
            out += name;
            out += "\":\"";
            appendEscaped(out, til::u16u8(value));
            out += '"';
        }

        void appendField(std::string& out, std::string_view name, uint32_t value)
        {
            if (value == 0)
            {
                return;
            }
            out += fmt::format(",\"{}\":{}", name, value);
        }

        std::string timestamp()
        {
            FILETIME ft{};
            GetSystemTimeAsFileTime(&ft);
            SYSTEMTIME st{};
            if (!FileTimeToSystemTime(&ft, &st))
            {
                return {};
            }
            return fmt::format("{:04}-{:02}-{:02}T{:02}:{:02}:{:02}.{:03}Z",
                               st.wYear,
                               st.wMonth,
                               st.wDay,
                               st.wHour,
                               st.wMinute,
                               st.wSecond,
                               st.wMilliseconds);
        }

        // Rotate when the file has grown past the cap. One generation is kept --
        // enough to span a restart, and JSONL means rotating is a rename rather
        // than a reparse.
        void rotateIfNeeded(const std::filesystem::path& path, uint32_t maxBytes) noexcept
        try
        {
            std::error_code ec;
            const auto size = std::filesystem::file_size(path, ec);
            if (ec || size <= maxBytes)
            {
                return;
            }

            auto rotated = path;
            rotated.replace_filename(RotatedFilename);
            std::filesystem::remove(rotated, ec);
            std::filesystem::rename(path, rotated, ec);
        }
        catch (...)
        {
            // A log that cannot rotate must not take the Terminal with it.
        }

        void writeBatch(std::string batch) noexcept
        try
        {
            if (batch.empty())
            {
                return;
            }

            const auto path = Path();
            if (path.empty())
            {
                return;
            }

            rotateIfNeeded(path, state().maxBytes.load(std::memory_order_relaxed));

            // FILE_APPEND_DATA without FILE_WRITE_DATA, and one WriteFile for the
            // whole batch: the OS appends at the end of the file as a single
            // operation, so two Terminal processes writing at once interleave
            // whole batches instead of corrupting a line. This is the reason the
            // log is not an ApplicationState field -- that rewrites the entire
            // document on every flush and has no cross-process story at all.
            wil::unique_hfile file{ CreateFileW(path.c_str(),
                                                FILE_APPEND_DATA,
                                                FILE_SHARE_READ | FILE_SHARE_WRITE,
                                                nullptr,
                                                OPEN_ALWAYS,
                                                FILE_ATTRIBUTE_NORMAL,
                                                nullptr) };
            if (!file)
            {
                return;
            }

            DWORD written{ 0 };
            WriteFile(file.get(), batch.data(), gsl::narrow_cast<DWORD>(batch.size()), &written, nullptr);
        }
        catch (...)
        {
        }

        void flushPending() noexcept
        try
        {
            std::string batch;
            {
                std::lock_guard guard{ state().mutex };
                batch.swap(state().pending);
            }
            writeBatch(std::move(batch));
        }
        catch (...)
        {
        }

        // Trailing, and deliberately NOT debouncing: debounce restarts the timer
        // on every call, so a steady stream of launches would postpone the write
        // indefinitely. ApplicationState can debounce because it only ever needs
        // the final value; a log needs all of them, reasonably soon.
        til::throttled_func<>& flusher()
        {
            static til::throttled_func<> f{
                til::throttled_func_options{ .delay = 1s, .debounce = false, .trailing = true },
                []() { flushPending(); }
            };
            return f;
        }
    }

    std::filesystem::path Path() noexcept
    try
    {
        static const auto path = []() -> std::filesystem::path {
            wil::unique_cotaskmem_string localAppData;
            // KF_FLAG_FORCE_APP_DATA_REDIRECTION gives the packaged app its own
            // LocalCache folder, which is how this lands beside settings.json and
            // stays separate per slot. Same call GetBaseSettingsPath() makes.
            if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_FORCE_APP_DATA_REDIRECTION, nullptr, &localAppData)))
            {
                return {};
            }

            std::filesystem::path dir{ localAppData.get() };
            if (!IsPackaged())
            {
                dir /= UnpackagedSettingsFolderName;
            }

            std::error_code ec;
            std::filesystem::create_directories(dir, ec);
            return dir / LogFilename;
        }();
        return path;
    }
    catch (...)
    {
        return {};
    }

    bool Enabled() noexcept
    {
        return state().enabled.load(std::memory_order_relaxed);
    }

    void Configure(bool enabled, uint32_t maxFileKilobytes) noexcept
    {
        // Clamp rather than trust: a zero cap would rotate on every single write.
        const auto kb = std::max<uint32_t>(64u, maxFileKilobytes);
        state().maxBytes.store(kb * 1024u, std::memory_order_relaxed);

        const auto was = state().enabled.exchange(enabled, std::memory_order_relaxed);
        if (was && !enabled)
        {
            // Turning it off should not silently drop what was already recorded.
            Flush();
        }
    }

    void Record(const Entry& entry) noexcept
    try
    {
        if (!Enabled())
        {
            return;
        }

        std::string line;
        line.reserve(512);
        line += "{\"ts\":\"";
        line += timestamp();
        line += "\",\"kind\":\"";
        appendEscaped(line, til::u16u8(entry.kind));
        line += '"';

        appendField(line, "exe", entry.exe);
        appendField(line, "commandLine", entry.commandLine);
        appendField(line, "cwd", entry.cwd);
        appendField(line, "pid", entry.pid);
        appendField(line, "parentPid", entry.parentPid);
        appendField(line, "parentExe", entry.parentExe);
        appendField(line, "profileName", entry.profileName);
        appendField(line, "profileGuid", entry.profileGuid);
        appendField(line, "sessionId", entry.sessionId);
        appendField(line, "reason", entry.reason);

        // Which Terminal wrote this. With two slots registered and several
        // windows open, "which process was this" is half the question.
        line += fmt::format(",\"loggerPid\":{}", GetCurrentProcessId());
        line += "}\n";

        {
            std::lock_guard guard{ state().mutex };
            state().pending += line;
        }

        flusher()();
    }
    catch (...)
    {
    }

    void Flush() noexcept
    {
        flushPending();
    }
}
