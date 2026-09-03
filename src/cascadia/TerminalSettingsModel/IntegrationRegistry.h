/*++
Copyright (c) Microsoft Corporation
Licensed under the MIT license.

Module Name:
- IntegrationRegistry.h

Abstract:
- Discovers integration manifests: the built-ins embedded in this DLL and the
  user's %LOCALAPPDATA%\Microsoft\Windows Terminal\Integrations directory.

--*/
#pragma once

#include "IntegrationRegistry.g.h"
#include "IntegrationManifest.h"

namespace winrt::Microsoft::Terminal::Settings::Model::implementation
{
    struct IntegrationRegistry
    {
        IntegrationRegistry() = default;

        static Windows::Foundation::Collections::IVector<Model::IntegrationManifest> All();
        static Model::IntegrationManifest Find(const hstring& id);
        static hstring UserDirectory();
        static void Refresh();
    };
}

namespace winrt::Microsoft::Terminal::Settings::Model::factory_implementation
{
    struct IntegrationRegistry : IntegrationRegistryT<IntegrationRegistry, implementation::IntegrationRegistry>
    {
    };
}
