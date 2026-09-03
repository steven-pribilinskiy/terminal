// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#pragma once

#include "IntegrationsViewModel.g.h"
#include "IntegrationViewModel.g.h"
#include "IntegrationSettingViewModel.g.h"
#include "IntegrationCredentialViewModel.g.h"
#include "IntegrationDisplayFieldViewModel.g.h"
#include "IntegrationMatcherViewModel.g.h"
#include "ViewModelHelpers.h"
#include "Utils.h"

namespace winrt::Microsoft::Terminal::Settings::Editor::implementation
{
    struct IntegrationSettingViewModel : IntegrationSettingViewModelT<IntegrationSettingViewModel>, ViewModelHelper<IntegrationSettingViewModel>
    {
    public:
        IntegrationSettingViewModel(Model::IntegrationField field,
                                    Model::GlobalAppSettings globalSettings,
                                    hstring integrationId);

        using ViewModelHelper<IntegrationSettingViewModel>::PropertyChanged;

        hstring Key() const { return _Field.Key(); }
        hstring Label() const;
        hstring Placeholder() const { return _Field.Placeholder(); }
        hstring Description() const { return _Field.Description(); }
        bool HasDescription() const { return !_Field.Description().empty(); }
        bool Required() const { return _Field.Required(); }

        hstring Value() const;
        void Value(const hstring& value);

    private:
        Model::IntegrationField _Field;
        Model::GlobalAppSettings _GlobalSettings;
        hstring _IntegrationId;
    };

    struct IntegrationCredentialViewModel : IntegrationCredentialViewModelT<IntegrationCredentialViewModel>, ViewModelHelper<IntegrationCredentialViewModel>
    {
    public:
        IntegrationCredentialViewModel(Model::IntegrationField field, hstring integrationId);

        using ViewModelHelper<IntegrationCredentialViewModel>::PropertyChanged;

        hstring Key() const { return _Field.Key(); }
        hstring Label() const;
        hstring Description() const { return _Field.Description(); }
        bool HasDescription() const { return !_Field.Description().empty(); }

        bool IsStored() const noexcept { return _isStored; }
        hstring StatusText() const;

        void Save(const hstring& value);
        void Clear();

        // NB: VIEW_MODEL_OBSERVABLE_PROPERTY ends with a `private:` label, which is
        // exactly what the members below want -- don't move it.
        VIEW_MODEL_OBSERVABLE_PROPERTY(hstring, PendingValue);

        Model::IntegrationField _Field;
        hstring _IntegrationId;
        bool _isStored{ false };

        void _refreshStored();
    };

    struct IntegrationDisplayFieldViewModel : IntegrationDisplayFieldViewModelT<IntegrationDisplayFieldViewModel>, ViewModelHelper<IntegrationDisplayFieldViewModel>
    {
    public:
        IntegrationDisplayFieldViewModel(Model::IntegrationDisplayField field,
                                         Model::IntegrationManifest manifest,
                                         Model::GlobalAppSettings globalSettings,
                                         hstring integrationId);

        using ViewModelHelper<IntegrationDisplayFieldViewModel>::PropertyChanged;

        hstring Key() const { return _Field.Key(); }
        hstring Label() const;

        bool Visible() const;
        void Visible(bool value);

    private:
        Model::IntegrationDisplayField _Field;
        Model::IntegrationManifest _Manifest;
        Model::GlobalAppSettings _GlobalSettings;
        hstring _IntegrationId;
    };

    struct IntegrationMatcherViewModel : IntegrationMatcherViewModelT<IntegrationMatcherViewModel>, ViewModelHelper<IntegrationMatcherViewModel>
    {
    public:
        IntegrationMatcherViewModel(Model::IntegrationMatcher matcher,
                                    Model::WindowSettings windowSettings,
                                    hstring integrationId,
                                    hstring integrationName);

        using ViewModelHelper<IntegrationMatcherViewModel>::PropertyChanged;

        hstring Description() const;
        hstring Pattern() const { return _Matcher.Pattern(); }

        hstring AddAsRule();

    private:
        Model::IntegrationMatcher _Matcher;
        Model::WindowSettings _WindowSettings;
        hstring _IntegrationId;
        hstring _IntegrationName;
    };

    struct IntegrationViewModel : IntegrationViewModelT<IntegrationViewModel>, ViewModelHelper<IntegrationViewModel>
    {
    public:
        IntegrationViewModel(Model::IntegrationManifest manifest,
                             Model::GlobalAppSettings globalSettings,
                             Model::WindowSettings windowSettings);

        using ViewModelHelper<IntegrationViewModel>::PropertyChanged;

        Model::IntegrationManifest Manifest() const noexcept { return _Manifest; }

        hstring Id() const { return _Manifest.Id(); }
        hstring Name() const;
        hstring Icon() const;
        hstring Source() const { return _Manifest.Source(); }
        bool IsBuiltIn() const { return _Manifest.IsBuiltIn(); }
        hstring AccessibleName() const;

        bool Enabled() const;
        void Enabled(bool value);

        bool IsConfigured() const noexcept { return _isConfigured; }
        bool IsNotConfigured() const noexcept { return !_isConfigured; }

        Windows::Foundation::Collections::IObservableVector<Editor::IntegrationSettingViewModel> Settings() const noexcept { return _Settings; }
        Windows::Foundation::Collections::IObservableVector<Editor::IntegrationCredentialViewModel> Credentials() const noexcept { return _Credentials; }
        Windows::Foundation::Collections::IObservableVector<Editor::IntegrationDisplayFieldViewModel> Fields() const noexcept { return _Fields; }
        Windows::Foundation::Collections::IObservableVector<Editor::IntegrationMatcherViewModel> SuggestedMatchers() const noexcept { return _SuggestedMatchers; }

        bool HasSettings() const noexcept { return _Settings.Size() > 0; }
        bool HasCredentials() const noexcept { return _Credentials.Size() > 0; }
        bool HasFields() const noexcept { return _Fields.Size() > 0; }
        bool HasSuggestedMatchers() const noexcept { return _SuggestedMatchers.Size() > 0; }

    private:
        Model::IntegrationManifest _Manifest;
        Model::GlobalAppSettings _GlobalSettings;
        Model::WindowSettings _WindowSettings;

        Windows::Foundation::Collections::IObservableVector<Editor::IntegrationSettingViewModel> _Settings;
        Windows::Foundation::Collections::IObservableVector<Editor::IntegrationCredentialViewModel> _Credentials;
        Windows::Foundation::Collections::IObservableVector<Editor::IntegrationDisplayFieldViewModel> _Fields;
        Windows::Foundation::Collections::IObservableVector<Editor::IntegrationMatcherViewModel> _SuggestedMatchers;

        // One per settings/credentials child, so a value typed into any of them
        // re-evaluates IsConfigured (and with it the "not configured" warning).
        std::vector<Windows::UI::Xaml::Data::INotifyPropertyChanged::PropertyChanged_revoker> _childRevokers;

        bool _isConfigured{ false };
        void _recomputeConfigured();
    };

    struct IntegrationsViewModel : IntegrationsViewModelT<IntegrationsViewModel>, ViewModelHelper<IntegrationsViewModel>
    {
    public:
        IntegrationsViewModel(Model::GlobalAppSettings globalSettings, Model::WindowSettings windowSettings);

        using ViewModelHelper<IntegrationsViewModel>::PropertyChanged;

        Windows::Foundation::Collections::IObservableVector<Editor::IntegrationViewModel> Integrations() const noexcept { return _Integrations; }
        bool NoIntegrations() const noexcept { return _Integrations.Size() == 0; }

        Editor::IntegrationViewModel CurrentIntegration() const noexcept { return _CurrentIntegration; }
        void CurrentIntegration(const Editor::IntegrationViewModel& vm)
        {
            _CurrentIntegration = vm;
            _NotifyChanges(L"CurrentIntegration", L"IsEditing", L"IsNotEditing");
        }
        bool IsEditing() const noexcept { return static_cast<bool>(_CurrentIntegration); }
        bool IsNotEditing() const noexcept { return !_CurrentIntegration; }

        hstring UserDirectory() const;

        void RequestNavigateToLinkTooltip();
        til::typed_event<> NavigateToLinkTooltipRequested;

    private:
        Model::GlobalAppSettings _GlobalSettings;
        Model::WindowSettings _WindowSettings;
        Windows::Foundation::Collections::IObservableVector<Editor::IntegrationViewModel> _Integrations;
        Editor::IntegrationViewModel _CurrentIntegration{ nullptr };
    };
}

namespace winrt::Microsoft::Terminal::Settings::Editor::factory_implementation
{
    // Only IntegrationsViewModel declares a constructor in the IDL; the rest are
    // built from C++ by their owner, so they have no activation factory.
    BASIC_FACTORY(IntegrationsViewModel);
}
