// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "pch.h"
#include "IntegrationCredentialStore.h"

#include <winrt/Windows.Security.Credentials.h>

#include "IntegrationCredentialStore.g.cpp"

using namespace winrt::Windows::Security::Credentials;

namespace
{
    // One vault "resource" per integration, one credential per field key. The
    // vault is per Windows user, so every Terminal built from this repo (and
    // both slots) sees the same credentials -- intended.
    winrt::hstring ResourceFor(const winrt::hstring& integrationId)
    {
        return winrt::hstring{ L"WindowsTerminal/Integrations/" } + integrationId;
    }

    PasswordCredential Find(const winrt::hstring& integrationId, const winrt::hstring& key)
    {
        try
        {
            PasswordVault vault;
            // Retrieve throws when nothing matches; that is the "not found" signal.
            return vault.Retrieve(ResourceFor(integrationId), key);
        }
        catch (...)
        {
            return nullptr;
        }
    }
}

namespace winrt::Microsoft::Terminal::Settings::Model::implementation
{
    hstring IntegrationCredentialStore::Get(const hstring& integrationId, const hstring& key)
    {
        if (const auto credential = Find(integrationId, key))
        {
            try
            {
                credential.RetrievePassword();
                return credential.Password();
            }
            CATCH_LOG();
        }
        return {};
    }

    void IntegrationCredentialStore::Set(const hstring& integrationId, const hstring& key, const hstring& value)
    {
        if (value.empty())
        {
            Remove(integrationId, key);
            return;
        }
        try
        {
            PasswordVault vault;
            if (const auto existing = Find(integrationId, key))
            {
                vault.Remove(existing);
            }
            vault.Add(PasswordCredential{ ResourceFor(integrationId), key, value });
        }
        CATCH_LOG();
    }

    void IntegrationCredentialStore::Remove(const hstring& integrationId, const hstring& key)
    {
        try
        {
            if (const auto existing = Find(integrationId, key))
            {
                PasswordVault vault;
                vault.Remove(existing);
            }
        }
        CATCH_LOG();
    }

    bool IntegrationCredentialStore::Has(const hstring& integrationId, const hstring& key)
    {
        return Find(integrationId, key) != nullptr;
    }
}
