/*++
Copyright (c) Microsoft Corporation
Licensed under the MIT license.

Module Name:
- IntegrationCredentialStore.h

Abstract:
- Integration credentials in the Windows credential vault (Credential Manager,
  "Web Credentials"). Nothing here ever reaches settings.json.

--*/
#pragma once

#include "IntegrationCredentialStore.g.h"

namespace winrt::Microsoft::Terminal::Settings::Model::implementation
{
    struct IntegrationCredentialStore
    {
        IntegrationCredentialStore() = default;

        static hstring Get(const hstring& integrationId, const hstring& key);
        static void Set(const hstring& integrationId, const hstring& key, const hstring& value);
        static void Remove(const hstring& integrationId, const hstring& key);
        static bool Has(const hstring& integrationId, const hstring& key);
    };
}

namespace winrt::Microsoft::Terminal::Settings::Model::factory_implementation
{
    struct IntegrationCredentialStore : IntegrationCredentialStoreT<IntegrationCredentialStore, implementation::IntegrationCredentialStore>
    {
    };
}
