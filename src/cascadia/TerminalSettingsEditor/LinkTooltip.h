// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#pragma once

#include "LinkTooltip.g.h"
#include "Utils.h"

namespace winrt::Microsoft::Terminal::Settings::Editor::implementation
{
    struct LinkTooltip : public HasScrollViewer<LinkTooltip>, LinkTooltipT<LinkTooltip>
    {
    public:
        LinkTooltip();

        void OnNavigatedTo(const winrt::Windows::UI::Xaml::Navigation::NavigationEventArgs& e);

        void AddRuleButton_Click(const winrt::Windows::Foundation::IInspectable& sender, const winrt::Microsoft::UI::Xaml::Controls::SplitButtonClickEventArgs& e);
        void AddRuleFlyout_Opening(const winrt::Windows::Foundation::IInspectable& sender, const winrt::Windows::Foundation::IInspectable& args);
        void ApplyPresetFlyout_Opening(const winrt::Windows::Foundation::IInspectable& sender, const winrt::Windows::Foundation::IInspectable& args);
        void DeleteRule_Click(const winrt::Windows::Foundation::IInspectable& sender, const winrt::Windows::UI::Xaml::RoutedEventArgs& e);
        void MoveRuleUp_Click(const winrt::Windows::Foundation::IInspectable& sender, const winrt::Windows::UI::Xaml::RoutedEventArgs& e);
        void MoveRuleDown_Click(const winrt::Windows::Foundation::IInspectable& sender, const winrt::Windows::UI::Xaml::RoutedEventArgs& e);
        void EditRule_Click(const winrt::Windows::Foundation::IInspectable& sender, const winrt::Windows::UI::Xaml::RoutedEventArgs& e);
        void CloseRuleEditor_Click(const winrt::Windows::Foundation::IInspectable& sender, const winrt::Windows::UI::Xaml::RoutedEventArgs& e);

        void AddCustomActionButton_Click(const winrt::Windows::Foundation::IInspectable& sender, const winrt::Windows::UI::Xaml::RoutedEventArgs& e);
        void DeleteCustomAction_Click(const winrt::Windows::Foundation::IInspectable& sender, const winrt::Windows::UI::Xaml::RoutedEventArgs& e);
        void CustomActionIconPicker_Loaded(const winrt::Windows::Foundation::IInspectable& sender, const winrt::Windows::UI::Xaml::RoutedEventArgs& e);

        Editor::IHostedInWindow WindowRoot() const noexcept { return _weakWindowRoot.get(); }
        WINRT_CALLBACK(PropertyChanged, Windows::UI::Xaml::Data::PropertyChangedEventHandler);
        WINRT_OBSERVABLE_PROPERTY(Editor::LinkTooltipViewModel, ViewModel, _PropertyChangedHandlers, nullptr);

    private:
        winrt::weak_ref<Editor::IHostedInWindow> _weakWindowRoot;
    };
}

namespace winrt::Microsoft::Terminal::Settings::Editor::factory_implementation
{
    BASIC_FACTORY(LinkTooltip);
}
