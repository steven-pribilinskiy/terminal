// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#pragma once

#include "SessionRestore.g.h"
#include "Utils.h"

namespace winrt::Microsoft::Terminal::Settings::Editor::implementation
{
    struct SessionRestore : public HasScrollViewer<SessionRestore>, SessionRestoreT<SessionRestore>
    {
    public:
        SessionRestore();

        void OnNavigatedTo(const winrt::Windows::UI::Xaml::Navigation::NavigationEventArgs& e);

        WINRT_CALLBACK(PropertyChanged, Windows::UI::Xaml::Data::PropertyChangedEventHandler);
        WINRT_OBSERVABLE_PROPERTY(Editor::SessionRestoreViewModel, ViewModel, _PropertyChangedHandlers, nullptr);
    };
}

namespace winrt::Microsoft::Terminal::Settings::Editor::factory_implementation
{
    BASIC_FACTORY(SessionRestore);
}
