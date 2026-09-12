// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.
//
// The Settings UI's own documentation. A list of topics, each one a markdown file
// packaged under Docs\ and rendered with the same renderer TerminalApp's markdown
// pane uses (Microsoft.Terminal.UI.Markdown).
//
// It exists because the three "Learn more" links in this editor used to leave the
// app, and two of the three answered a question about a feature upstream does not
// have, so the page they pointed at could not have described it. Everything this
// fork adds has no documentation anywhere else at all.
//
// Back navigation is the editor's breadcrumb, not a button: opening a topic appends
// a crumb, and clicking the "Documentation" crumb re-navigates the frame, which
// builds a fresh page showing the list again. That is why this page holds no
// "current topic" state worth restoring.

#pragma once

#include "Documentation.g.h"
#include "Utils.h"

namespace winrt::Microsoft::Terminal::Settings::Editor::implementation
{
    struct Documentation : public HasScrollViewer<Documentation>, DocumentationT<Documentation>
    {
    public:
        Documentation();

        void OnNavigatedTo(const winrt::Windows::UI::Xaml::Navigation::NavigationEventArgs& e);

        void TopicCard_Click(const winrt::Windows::Foundation::IInspectable& sender,
                             const winrt::Windows::UI::Xaml::RoutedEventArgs& e);

        Editor::IHostedInWindow WindowRoot() const noexcept { return _weakWindowRoot.get(); }

        // Public because a Hyperlink inside a rendered topic calls it through a weak
        // reference to this page, to follow a link from one topic to another.
        void OpenTopic(const winrt::hstring& id);

    private:
        winrt::weak_ref<Editor::IHostedInWindow> _weakWindowRoot;

        // True once this page has put a crumb of its own on the trail, so a
        // topic-to-topic link replaces that crumb instead of stacking a second one.
        bool _pushedCrumb{ false };

        void _showCrumb(const winrt::hstring& label);
        void _scrollToTop();
    };
}

namespace winrt::Microsoft::Terminal::Settings::Editor::factory_implementation
{
    BASIC_FACTORY(Documentation);
}
