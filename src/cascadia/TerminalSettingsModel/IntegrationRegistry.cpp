// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "pch.h"
#include "IntegrationRegistry.h"
#include "CascadiaSettings.h"
#include "resource.h"

#include <shlobj.h>
#include <til/io.h>

#include "IntegrationRegistry.g.cpp"

using namespace winrt::Windows::Foundation::Collections;

namespace
{
    constexpr std::wstring_view IntegrationsPath{ L"\\Microsoft\\Windows Terminal\\Integrations" };
    constexpr std::wstring_view ManifestFilename{ L"integration.json" };
    constexpr std::wstring_view BuiltInSource{ L"built-in" };

    struct RegistryState
    {
        std::vector<winrt::Microsoft::Terminal::Settings::Model::IntegrationManifest> manifests;
        bool loaded = false;
    };

    til::shared_mutex<RegistryState>& State()
    {
        static til::shared_mutex<RegistryState> state;
        return state;
    }

    std::filesystem::path UserIntegrationsDirectory()
    {
        wil::unique_cotaskmem_string folder;
        THROW_IF_FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &folder));
        std::wstring path{ folder.get() };
        path.append(IntegrationsPath);
        return std::filesystem::path{ std::move(path) };
    }

    void AddOrReplace(std::vector<winrt::Microsoft::Terminal::Settings::Model::IntegrationManifest>& manifests,
                      const winrt::Microsoft::Terminal::Settings::Model::IntegrationManifest& manifest)
    {
        for (auto& existing : manifests)
        {
            if (existing.Id() == manifest.Id())
            {
                existing = manifest;
                return;
            }
        }
        manifests.push_back(manifest);
    }

    void LoadBuiltIn(std::vector<winrt::Microsoft::Terminal::Settings::Model::IntegrationManifest>& manifests, int resourceId)
    try
    {
        using namespace winrt::Microsoft::Terminal::Settings::Model::implementation;
        const auto text = LoadStringResource(resourceId);
        if (const auto manifest = IntegrationManifest::FromString(text, winrt::hstring{ BuiltInSource }, true))
        {
            AddOrReplace(manifests, *manifest);
        }
    }
    CATCH_LOG()

    void LoadUserManifest(std::vector<winrt::Microsoft::Terminal::Settings::Model::IntegrationManifest>& manifests, const std::filesystem::path& file)
    try
    {
        using namespace winrt::Microsoft::Terminal::Settings::Model::implementation;
        const auto text = til::io::read_file_as_utf8_string_if_exists(file);
        if (text.empty())
        {
            return;
        }
        if (const auto manifest = IntegrationManifest::FromString(text, winrt::hstring{ file.native() }, false))
        {
            AddOrReplace(manifests, *manifest);
        }
    }
    CATCH_LOG()

    void LoadAll(RegistryState& state)
    {
        std::vector<winrt::Microsoft::Terminal::Settings::Model::IntegrationManifest> manifests;

        LoadBuiltIn(manifests, IDR_INTEGRATION_JIRA);
        LoadBuiltIn(manifests, IDR_INTEGRATION_SLACK);
        LoadBuiltIn(manifests, IDR_INTEGRATION_STITH);
        LoadBuiltIn(manifests, IDR_INTEGRATION_GITHUB);

        try
        {
            const auto directory = UserIntegrationsDirectory();
            if (std::filesystem::is_directory(directory))
            {
                for (const auto& entry : std::filesystem::directory_iterator{ directory })
                {
                    if (entry.is_directory())
                    {
                        LoadUserManifest(manifests, entry.path() / ManifestFilename);
                    }
                    else if (entry.is_regular_file() && entry.path().extension() == L".json")
                    {
                        LoadUserManifest(manifests, entry.path());
                    }
                }
            }
        }
        CATCH_LOG();

        state.manifests = std::move(manifests);
        state.loaded = true;
    }
}

namespace winrt::Microsoft::Terminal::Settings::Model::implementation
{
    IVector<Model::IntegrationManifest> IntegrationRegistry::All()
    {
        {
            const auto state = State().lock_shared();
            if (state->loaded)
            {
                return winrt::single_threaded_vector<Model::IntegrationManifest>(std::vector{ state->manifests });
            }
        }
        Refresh();
        const auto state = State().lock_shared();
        return winrt::single_threaded_vector<Model::IntegrationManifest>(std::vector{ state->manifests });
    }

    Model::IntegrationManifest IntegrationRegistry::Find(const hstring& id)
    {
        for (const auto& manifest : All())
        {
            if (manifest.Id() == id)
            {
                return manifest;
            }
        }
        return nullptr;
    }

    hstring IntegrationRegistry::UserDirectory()
    {
        try
        {
            return hstring{ UserIntegrationsDirectory().native() };
        }
        catch (...)
        {
            return {};
        }
    }

    void IntegrationRegistry::Refresh()
    {
        const auto state = State().lock();
        LoadAll(*state);
    }
}
