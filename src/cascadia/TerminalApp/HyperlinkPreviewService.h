/*++
Copyright (c) Microsoft Corporation
Licensed under the MIT license.

Module Name:
- HyperlinkPreviewService.h

Abstract:
- The engine behind the hyperlink card's link previews: it runs the integration
  manifests the settings model discovered.

  One instance lives per TerminalPage and is handed to every TermControl the
  page creates, because credentials and settings belong to the app, not to a
  control. On a settings reload the same object is rebuilt in place, so the
  controls never need to be told about it again.

  Rebuild() takes the snapshot -- enabled integrations, their setting values,
  their credentials (read from the vault exactly ONCE, here, and never on a
  hover), and the compiled ICU regexes of their matchers. Everything after that
  reads the snapshot and never touches the settings model again.

  The compiled regexes are why the snapshot type is only forward-declared here:
  <icu.h> is not something TerminalPage.h should drag into every translation
  unit that includes it.

--*/
#pragma once

#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <string>

namespace winrt::TerminalApp::implementation
{
    // Defined in HyperlinkPreviewService.cpp.
    struct HyperlinkPreviewSnapshot;

    struct HyperlinkPreviewService : winrt::implements<HyperlinkPreviewService, winrt::Microsoft::Terminal::Control::IHyperlinkPreviewProvider>
    {
        HyperlinkPreviewService() = default;

        // Re-reads the enabled integrations and the window's text-match rules.
        // Safe to call from the UI thread at any time; in-flight fetches keep
        // running against the snapshot they started with.
        void Rebuild(const winrt::Microsoft::Terminal::Settings::Model::CascadiaSettings& settings,
                     const winrt::Microsoft::Terminal::Settings::Model::WindowSettings& windowSettings);

        // IHyperlinkPreviewProvider
        winrt::hstring ResolveLink(const winrt::hstring& text);
        bool CanPreview(const winrt::hstring& text, const winrt::hstring& integrationHint);
        winrt::Windows::Foundation::IAsyncOperation<winrt::Microsoft::Terminal::Control::HyperlinkPreview> GetPreviewAsync(winrt::hstring text, winrt::hstring integrationHint);
        winrt::Windows::Foundation::IAsyncOperation<winrt::Microsoft::Terminal::Control::HyperlinkPreview> RefreshAsync(winrt::hstring text, winrt::hstring integrationHint);
        winrt::Windows::Foundation::IAsyncOperation<winrt::Microsoft::Terminal::Control::HyperlinkActionResult> InvokeActionAsync(winrt::hstring text,
                                                                                                                                 winrt::hstring integrationHint,
                                                                                                                                 winrt::hstring actionKey,
                                                                                                                                 winrt::hstring choiceId,
                                                                                                                                 winrt::Windows::Foundation::Collections::IMap<winrt::hstring, winrt::hstring> fieldValues);

    private:
        struct CacheEntry
        {
            winrt::Microsoft::Terminal::Control::HyperlinkPreview Preview{ nullptr };
            std::chrono::steady_clock::time_point Expiry{};
        };

        // GetPreviewAsync and RefreshAsync differ only in whether they are
        // allowed to answer from the cache.
        winrt::Windows::Foundation::IAsyncOperation<winrt::Microsoft::Terminal::Control::HyperlinkPreview> _previewAsync(winrt::hstring text,
                                                                                                                        winrt::hstring integrationHint,
                                                                                                                        bool bypassCache);

        std::shared_ptr<const HyperlinkPreviewSnapshot> _currentSnapshot() const;
        winrt::Microsoft::Terminal::Control::HyperlinkPreview _cacheLookup(const std::wstring& key);
        void _cacheStore(const std::wstring& key, const winrt::Microsoft::Terminal::Control::HyperlinkPreview& preview, int32_t seconds);
        void _cacheErase(const std::wstring& key);

        mutable std::mutex _mutex;
        std::shared_ptr<const HyperlinkPreviewSnapshot> _snapshot;
        std::map<std::wstring, CacheEntry> _cache;
    };
}
