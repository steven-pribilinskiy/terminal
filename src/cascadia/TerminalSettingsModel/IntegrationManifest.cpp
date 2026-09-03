// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "pch.h"
#include "IntegrationManifest.h"
#include "JsonUtils.h"
#include "TerminalSettingsSerializationHelpers.h"

#include "IntegrationField.g.cpp"
#include "IntegrationMatcher.g.cpp"
#include "IntegrationFetchStep.g.cpp"
#include "IntegrationDisplayField.g.cpp"
#include "IntegrationManifest.g.cpp"

using namespace Microsoft::Terminal::Settings::Model;
using namespace winrt::Windows::Foundation::Collections;

namespace
{
    constexpr std::string_view KeyKey{ "key" };
    constexpr std::string_view LabelKey{ "label" };
    constexpr std::string_view PlaceholderKey{ "placeholder" };
    constexpr std::string_view DescriptionKey{ "description" };
    constexpr std::string_view RequiredKey{ "required" };
    constexpr std::string_view SecretKey{ "secret" };

    constexpr std::string_view KindKey{ "kind" };
    constexpr std::string_view PatternKey{ "pattern" };
    constexpr std::string_view HostSettingKey{ "hostSetting" };
    constexpr std::string_view LinkKey{ "link" };
    constexpr std::string_view SuggestedKey{ "suggested" };

    constexpr std::string_view IdKey{ "id" };
    constexpr std::string_view TypeKey{ "type" };
    constexpr std::string_view UrlKey{ "url" };
    constexpr std::string_view MethodKey{ "method" };
    constexpr std::string_view HeadersKey{ "headers" };
    constexpr std::string_view AuthKey{ "auth" };
    constexpr std::string_view AuthUserKey{ "user" };
    constexpr std::string_view AuthPasswordKey{ "password" };
    constexpr std::string_view AuthValueKey{ "value" };
    constexpr std::string_view AuthHeaderKey{ "header" };
    constexpr std::string_view BodyKey{ "body" };
    constexpr std::string_view AllowUntrustedCertificateKey{ "allowUntrustedCertificate" };
    constexpr std::string_view CommandLineKey{ "commandLine" };
    constexpr std::string_view StdinKey{ "stdin" };
    constexpr std::string_view TimeoutMsKey{ "timeoutMs" };
    constexpr std::string_view WhenKey{ "when" };
    constexpr std::string_view UnlessKey{ "unless" };

    constexpr std::string_view PathKey{ "path" };
    constexpr std::string_view IconPathKey{ "iconPath" };
    constexpr std::string_view ColorPathKey{ "colorPath" };
    constexpr std::string_view ColorKey{ "color" };
    constexpr std::string_view FormatKey{ "format" };
    constexpr std::string_view DefaultKey{ "default" };

    constexpr std::string_view NameKey{ "name" };
    constexpr std::string_view IconKey{ "icon" };
    constexpr std::string_view VersionKey{ "version" };
    constexpr std::string_view CacheSecondsKey{ "cacheSeconds" };
    constexpr std::string_view HtmlKey{ "html" };
    constexpr std::string_view SettingsKey{ "settings" };
    constexpr std::string_view CredentialsKey{ "credentials" };
    constexpr std::string_view MatchersKey{ "matchers" };
    constexpr std::string_view FetchKey{ "fetch" };
    constexpr std::string_view FieldsKey{ "fields" };
}

namespace winrt::Microsoft::Terminal::Settings::Model::implementation
{
    com_ptr<IntegrationField> IntegrationField::FromJson(const Json::Value& json)
    {
        auto field = winrt::make_self<IntegrationField>();
        JsonUtils::GetValueForKey(json, KeyKey, field->_Key);
        JsonUtils::GetValueForKey(json, LabelKey, field->_Label);
        JsonUtils::GetValueForKey(json, PlaceholderKey, field->_Placeholder);
        JsonUtils::GetValueForKey(json, DescriptionKey, field->_Description);
        JsonUtils::GetValueForKey(json, RequiredKey, field->_Required);
        JsonUtils::GetValueForKey(json, SecretKey, field->_Secret);
        if (field->_Label.empty())
        {
            field->_Label = field->_Key;
        }
        return field;
    }

    com_ptr<IntegrationMatcher> IntegrationMatcher::FromJson(const Json::Value& json)
    {
        auto matcher = winrt::make_self<IntegrationMatcher>();
        JsonUtils::GetValueForKey(json, KindKey, matcher->_Kind);
        JsonUtils::GetValueForKey(json, PatternKey, matcher->_Pattern);
        JsonUtils::GetValueForKey(json, HostSettingKey, matcher->_HostSetting);
        JsonUtils::GetValueForKey(json, LinkKey, matcher->_LinkTemplate);
        JsonUtils::GetValueForKey(json, SuggestedKey, matcher->_Suggested);
        JsonUtils::GetValueForKey(json, DescriptionKey, matcher->_Description);
        return matcher;
    }

    com_ptr<IntegrationFetchStep> IntegrationFetchStep::FromJson(const Json::Value& json)
    {
        auto step = winrt::make_self<IntegrationFetchStep>();
        JsonUtils::GetValueForKey(json, IdKey, step->_Id);
        JsonUtils::GetValueForKey(json, TypeKey, step->_Type);
        JsonUtils::GetValueForKey(json, UrlKey, step->_Url);
        JsonUtils::GetValueForKey(json, MethodKey, step->_Method);
        JsonUtils::GetValueForKey(json, HeadersKey, step->_Headers);
        JsonUtils::GetValueForKey(json, BodyKey, step->_Body);
        JsonUtils::GetValueForKey(json, AllowUntrustedCertificateKey, step->_AllowUntrustedCertificate);
        JsonUtils::GetValueForKey(json, CommandLineKey, step->_CommandLine);
        JsonUtils::GetValueForKey(json, StdinKey, step->_Stdin);
        JsonUtils::GetValueForKey(json, TimeoutMsKey, step->_TimeoutMs);
        JsonUtils::GetValueForKey(json, WhenKey, step->_When);
        JsonUtils::GetValueForKey(json, UnlessKey, step->_Unless);

        // "auth": { "type": "basic", "user": "…", "password": "…" }
        //         { "type": "bearer", "value": "…" }
        //         { "type": "header", "header": "X-Api-Key", "value": "…" }
        if (const auto auth = json.find(AuthKey.data(), AuthKey.data() + AuthKey.size()); auth && auth->isObject())
        {
            JsonUtils::GetValueForKey(*auth, TypeKey, step->_AuthType);
            if (step->_AuthType == L"basic")
            {
                JsonUtils::GetValueForKey(*auth, AuthUserKey, step->_AuthUser);
                JsonUtils::GetValueForKey(*auth, AuthPasswordKey, step->_AuthPassword);
            }
            else if (step->_AuthType == L"header")
            {
                JsonUtils::GetValueForKey(*auth, AuthHeaderKey, step->_AuthUser);
                JsonUtils::GetValueForKey(*auth, AuthValueKey, step->_AuthPassword);
            }
            else
            {
                JsonUtils::GetValueForKey(*auth, AuthValueKey, step->_AuthPassword);
            }
        }

        if (step->_Method.empty())
        {
            step->_Method = L"GET";
        }
        if (step->_TimeoutMs <= 0)
        {
            step->_TimeoutMs = 8000;
        }
        return step;
    }

    com_ptr<IntegrationDisplayField> IntegrationDisplayField::FromJson(const Json::Value& json)
    {
        auto field = winrt::make_self<IntegrationDisplayField>();
        JsonUtils::GetValueForKey(json, KeyKey, field->_Key);
        JsonUtils::GetValueForKey(json, LabelKey, field->_Label);
        JsonUtils::GetValueForKey(json, PathKey, field->_Path);
        JsonUtils::GetValueForKey(json, KindKey, field->_Kind);
        JsonUtils::GetValueForKey(json, IconPathKey, field->_IconPath);
        JsonUtils::GetValueForKey(json, ColorPathKey, field->_ColorPath);
        JsonUtils::GetValueForKey(json, ColorKey, field->_Color);
        JsonUtils::GetValueForKey(json, FormatKey, field->_Format);
        JsonUtils::GetValueForKey(json, DefaultKey, field->_DefaultVisible);
        if (field->_Key.empty())
        {
            field->_Key = field->_Label;
        }
        return field;
    }

    com_ptr<IntegrationManifest> IntegrationManifest::FromJson(const Json::Value& json, const hstring& source, bool isBuiltIn)
    {
        if (!json.isObject())
        {
            return nullptr;
        }

        auto manifest = winrt::make_self<IntegrationManifest>();
        manifest->_Source = source;
        manifest->_IsBuiltIn = isBuiltIn;

        JsonUtils::GetValueForKey(json, IdKey, manifest->_Id);
        JsonUtils::GetValueForKey(json, NameKey, manifest->_Name);
        JsonUtils::GetValueForKey(json, IconKey, manifest->_Icon);
        JsonUtils::GetValueForKey(json, VersionKey, manifest->_Version);
        JsonUtils::GetValueForKey(json, CacheSecondsKey, manifest->_CacheSeconds);
        JsonUtils::GetValueForKey(json, HtmlKey, manifest->_Html);
        JsonUtils::GetValueForKey(json, SettingsKey, manifest->_Settings);
        JsonUtils::GetValueForKey(json, CredentialsKey, manifest->_Credentials);
        JsonUtils::GetValueForKey(json, MatchersKey, manifest->_Matchers);
        JsonUtils::GetValueForKey(json, FetchKey, manifest->_FetchSteps);
        JsonUtils::GetValueForKey(json, FieldsKey, manifest->_Fields);

        if (manifest->_Id.empty())
        {
            return nullptr;
        }
        if (manifest->_Name.empty())
        {
            manifest->_Name = manifest->_Id;
        }
        if (manifest->_CacheSeconds < 0)
        {
            manifest->_CacheSeconds = 0;
        }

        // Every collection is non-null so consumers can iterate without checks.
        if (!manifest->_Settings)
        {
            manifest->_Settings = winrt::single_threaded_vector<Model::IntegrationField>();
        }
        if (!manifest->_Credentials)
        {
            manifest->_Credentials = winrt::single_threaded_vector<Model::IntegrationField>();
        }
        if (!manifest->_Matchers)
        {
            manifest->_Matchers = winrt::single_threaded_vector<Model::IntegrationMatcher>();
        }
        if (!manifest->_FetchSteps)
        {
            manifest->_FetchSteps = winrt::single_threaded_vector<Model::IntegrationFetchStep>();
        }
        if (!manifest->_Fields)
        {
            manifest->_Fields = winrt::single_threaded_vector<Model::IntegrationDisplayField>();
        }
        // Credentials are secret unless the manifest says otherwise.
        for (const auto& credential : manifest->_Credentials)
        {
            if (!credential.Secret() && credential.Key() != L"email" && credential.Key() != L"user" && credential.Key() != L"username")
            {
                credential.Secret(true);
            }
        }
        return manifest;
    }

    com_ptr<IntegrationManifest> IntegrationManifest::FromString(std::string_view text, const hstring& source, bool isBuiltIn)
    {
        Json::Value root;
        std::string errs;
        const std::unique_ptr<Json::CharReader> reader{ Json::CharReaderBuilder{}.newCharReader() };
        if (!reader->parse(text.data(), text.data() + text.size(), &root, &errs))
        {
            return nullptr;
        }
        return FromJson(root, source, isBuiltIn);
    }
}
