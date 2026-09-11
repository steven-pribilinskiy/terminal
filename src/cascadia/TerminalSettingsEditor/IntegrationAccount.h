// Account checks run on a background thread; only public identity data returns to the UI.
#pragma once
#include "../../inc/GitHubAuthentication.h"
#include <winrt/Windows.Web.Http.h>
#include <winrt/Windows.Web.Http.Filters.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Security.Cryptography.h>
#include <map>
#include <set>

namespace IntegrationAccounts
{
    namespace J = winrt::Windows::Data::Json;
    namespace H = winrt::Windows::Web::Http;
    using Values = std::map<std::wstring, std::wstring>;
    struct Result
    {
        bool connected{};
        std::wstring name, login, avatar, link, source, message, discoveryMessage;
        std::vector<std::wstring> organizations;
    };
    inline std::wstring Value(const Values& values, const std::wstring& key)
    {
        const auto it = values.find(key); return it == values.end() ? std::wstring{} : it->second;
    }
    inline std::wstring String(const J::JsonObject& object, const wchar_t* key)
    {
        if (!object || !object.HasKey(key)) return {};
        const auto value = object.Lookup(key);
        return value.ValueType() == J::JsonValueType::String ? std::wstring{ value.GetString() } : std::wstring{};
    }
    using ::Microsoft::Terminal::ValidGitHubOwner;
    inline std::vector<std::wstring> Owners(std::wstring_view value) { return ::Microsoft::Terminal::GitHubOwners(value); }
    inline Result Check(std::wstring provider, Values settings, Values credentials, bool discover)
    {
        Result result;
        if (provider != L"github" && provider != L"jira" && provider != L"slack") { result.message = L"This integration does not provide an account check"; return result; }
        try
        {
            auto token = Value(credentials, L"token");
            if (provider == L"github")
            {
                const auto cli = ::Microsoft::Terminal::GitHubToken({});
                if (!cli.empty()) { token = cli; result.source = L"GitHub CLI"; }
                else result.source = L"Personal access token";
                if (token.empty()) { result.message = L"Sign in with GitHub CLI or save a personal access token"; return result; }
            }
            std::wstring authorization = L"Bearer " + token;
            if (provider == L"jira")
            {
                using B = winrt::Windows::Security::Cryptography::CryptographicBuffer;
                authorization = L"Basic " + std::wstring{ B::EncodeToBase64String(B::ConvertStringToBinary(Value(credentials, L"email") + L":" + token, winrt::Windows::Security::Cryptography::BinaryStringEncoding::Utf8)) };
                result.source = L"API token";
            }
            H::Filters::HttpBaseProtocolFilter filter;
            filter.AllowAutoRedirect(false);
            H::HttpClient client{ filter };
            const auto get = [&](const std::wstring& url) -> J::IJsonValue {
                H::HttpRequestMessage request{ H::HttpMethod::Get(), winrt::Windows::Foundation::Uri{ url } };
                request.Headers().TryAppendWithoutValidation(L"Authorization", authorization);
                request.Headers().TryAppendWithoutValidation(L"Accept", L"application/json");
                request.Headers().TryAppendWithoutValidation(L"User-Agent", L"WindowsTerminal");
                auto operation = client.SendRequestAsync(request);
                if (operation.wait_for(std::chrono::seconds{ 8 }) != winrt::Windows::Foundation::AsyncStatus::Completed) { operation.Cancel(); throw winrt::hresult_error(E_FAIL); }
                const auto response = operation.get();
                if (response.StatusCode() != H::HttpStatusCode::Ok) throw winrt::hresult_error(E_ACCESSDENIED);
                auto read = response.Content().ReadAsStringAsync();
                if (read.wait_for(std::chrono::seconds{ 8 }) != winrt::Windows::Foundation::AsyncStatus::Completed) { read.Cancel(); throw winrt::hresult_error(E_FAIL); }
                const auto text = read.get();
                if (text.size() > 4 * 1024 * 1024) throw winrt::hresult_error(E_FAIL);
                return J::JsonValue::Parse(text);
            };
            if (provider == L"github")
            {
                const auto user = get(L"https://api.github.com/user").GetObject();
                result.login = String(user, L"login"); result.name = String(user, L"name");
                result.avatar = String(user, L"avatar_url"); result.link = String(user, L"html_url");
                if (result.login.empty()) throw winrt::hresult_error(E_FAIL);
                if (discover)
                {
                    std::set<std::wstring> orgs;
                    bool incomplete = false;
                    for (const auto resource : { L"orgs", L"repos" })
                    {
                        try
                        {
                            for (int page = 1; page <= 3; ++page)
                            {
                                const auto rows = get(L"https://api.github.com/user/" + std::wstring{ resource } + L"?per_page=100&page=" + std::to_wstring(page)).GetArray();
                                for (const auto& row : rows)
                                {
                                    auto org = row.GetObject();
                                    if (std::wstring_view{ resource } == L"repos")
                                    {
                                        org = org.GetNamedObject(L"owner", nullptr);
                                        if (String(org, L"type") != L"Organization") continue;
                                    }
                                    const auto login = String(org, L"login");
                                    if (ValidGitHubOwner(login)) orgs.insert(login);
                                }
                                if (rows.Size() < 100) break;
                                if (page == 3) incomplete = true;
                            }
                        } catch (...) { incomplete = true; }
                    }
                    result.organizations.assign(orgs.begin(), orgs.end());
                    result.discoveryMessage = incomplete ? L"Some organizations may be missing because of token permissions or the discovery limit. You can add them manually." : L"Organizations visible to the active credentials";
                }
            }
            else if (provider == L"jira")
            {
                auto host = Value(settings, L"host");
                if (host.find(L"://") == std::wstring::npos) host = L"https://" + host;
                const auto uri = winrt::Windows::Foundation::Uri{ host };
                host = std::wstring{ uri.Host() };
                const auto user = get(L"https://" + host + L"/rest/api/3/myself").GetObject();
                result.name = String(user, L"displayName"); result.login = String(user, L"emailAddress");
                const auto id = String(user, L"accountId").empty() ? String(user, L"name") : String(user, L"accountId");
                if (id.empty()) throw winrt::hresult_error(E_FAIL);
                if (result.login.empty()) result.login = id;
                result.avatar = String(user.GetNamedObject(L"avatarUrls", nullptr), L"48x48");
                result.link = L"https://" + host + L"/jira/people/" + std::wstring{ winrt::Windows::Foundation::Uri::EscapeComponent(id) };
            }
            else
            {
                result.source = L"Slack token";
                const auto user = get(L"https://slack.com/api/auth.test").GetObject();
                if (!user.GetNamedBoolean(L"ok", false)) throw winrt::hresult_error(E_ACCESSDENIED);
                const auto id = String(user, L"user_id"); if (id.empty()) throw winrt::hresult_error(E_FAIL);
                result.name = String(user, L"user"); result.login = String(user, L"team"); result.link = String(user, L"url");
                try
                {
                    const auto details = get(L"https://slack.com/api/users.info?user=" + std::wstring{ winrt::Windows::Foundation::Uri::EscapeComponent(id) }).GetObject();
                    const auto profile = details.GetNamedObject(L"user").GetNamedObject(L"profile");
                    const auto name = String(profile, L"display_name"); if (!name.empty()) result.name = name;
                    result.avatar = String(profile, L"image_72");
                } catch (...) { /* auth.test verified identity; profile permissions are optional. */ }
            }
            if (result.name.empty()) result.name = result.login;
            result.connected = true; result.message = L"Connected";
        }
        catch (...) { result.message = L"Could not verify the connection. Check credentials, permissions and network, then retry."; }
        return result;
    }
}
