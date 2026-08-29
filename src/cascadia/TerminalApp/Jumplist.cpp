// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "pch.h"
#include "Jumplist.h"
#include "JumplistIconCache.h"

#include <ShObjIdl.h>
#include <Propkey.h>

#include <WtExeUtils.h>

#include "../../types/inc/utils.hpp"

using namespace winrt::Microsoft::Terminal::Settings::Model;

//  This property key isn't already defined in propkey.h, but is used by UWP Jumplist to determine the icon of the jumplist item.
//  IShellLink's SetIconLocation isn't going to read "ms-appx://" icon paths, so we'll need to use this to set the icon.
DEFINE_PROPERTYKEY(PKEY_AppUserModel_DestListLogoUri, 0x9F4C2855, 0x9F79, 0x4B39, 0xA8, 0xD0, 0xE1, 0xD4, 0x2D, 0xE1, 0xD5, 0xF3, 29);
#define INIT_PKEY_AppUserModel_DestListLogoUri                                             \
    {                                                                                      \
        { 0x9F4C2855, 0x9F79, 0x4B39, 0xA8, 0xD0, 0xE1, 0xD4, 0x2D, 0xE1, 0xD5, 0xF3 }, 29 \
    }

// Method Description:
// - Updates the items of the Jumplist based on the given settings.
// Arguments:
// - settings - The settings object to update the jumplist with.
// Return Value:
// - <none>
safe_void_coroutine Jumplist::UpdateJumplist(const CascadiaSettings& settings) noexcept
{
    if (!settings)
    {
        // By all accounts, this shouldn't be null. Seemingly however (GH
        // #12360), it sometimes is. So just check this case here and log a
        // message.
        TraceLoggingWrite(g_hTerminalAppProvider,
                          "Jumplist_UpdateJumplist_NullSettings",
                          TraceLoggingLevel(WINEVENT_LEVEL_VERBOSE),
                          TraceLoggingKeyword(TIL_KEYWORD_TRACE));

        co_return;
    }

    // make sure to capture the settings _before_ the co_await
    const auto strongSettings = settings;

    // Explorer APIs may block, so do it on a background thread.
    //
    // NOTE: Jumplist has no members, so we don't need to hold onto `this` here.
    co_await winrt::resume_background();

    try
    {
        auto jumplistInstance = winrt::create_instance<ICustomDestinationList>(CLSID_DestinationList, CLSCTX_ALL);

        // Start the Jumplist edit transaction
        uint32_t slots;
        winrt::com_ptr<IObjectCollection> jumplistItems;
        jumplistItems.capture(jumplistInstance, &ICustomDestinationList::BeginList, &slots);

        // Update the list of profiles.
        _updateProfiles(jumplistItems.get(), strongSettings.ActiveProfiles().GetView());

        // TODO GH#1571: Add items from the future customizable new tab dropdown as well.
        // This could either replace the default profiles, or be added alongside them.

        // Add the items to the jumplist Task section.
        // The Tasks section is immutable by the user, unlike the destinations
        // section that can have its items pinned and removed.
        THROW_IF_FAILED(jumplistInstance->AddUserTasks(jumplistItems.get()));

        THROW_IF_FAILED(jumplistInstance->CommitList());
    }
    CATCH_LOG();
}

// Method Description:
// - Creates and adds a ShellLink object to the Jumplist for each profile.
// Arguments:
// - jumplistItems - The jumplist item list
// - profiles - The profiles to add to the jumplist
// Return Value:
// - S_OK or HRESULT failure code.
void Jumplist::_updateProfiles(IObjectCollection* jumplistItems, winrt::Windows::Foundation::Collections::IVectorView<Profile> profiles)
{
    // It's easier to clear the list and re-add everything. The settings aren't
    // updated often, and there likely isn't a huge amount of items to add.
    THROW_IF_FAILED(jumplistItems->Clear());

    // Qualified: `TerminalApp` alone resolves to the WinRT projection namespace
    // here, not to ours. Same reason AppLogic.cpp writes ::TerminalApp::SlotPromotion.
    ::TerminalApp::JumplistIconCache iconCache;

    for (const auto& profile : profiles)
    {
        // Craft the arguments following "wt.exe"
        auto args = fmt::format(FMT_COMPILE(L"-p {}"), to_hstring(profile.Guid()));

        std::wstring normalizedIconPath{ profile.Icon().Resolved() };

        // A glyph or emoji resolves to itself (by design -- see
        // MediaResourceSupport.h), and neither SetIconLocation nor
        // DestListLogoUri can draw one, so the entry would come out blank.
        // Rasterize it to an .ico and point at that instead. On failure we
        // leave the string alone; _createShellLink falls back to our own icon.
        if (::Microsoft::Console::Utils::IsLikelyToBeEmojiOrSymbolIcon(normalizedIconPath))
        {
            if (auto rasterized{ iconCache.Ensure(normalizedIconPath) })
            {
                normalizedIconPath = std::move(*rasterized);
            }
        }

        // Create the shell link object for the profile
        const auto shLink = _createShellLink(profile.Name(), normalizedIconPath, args);
        THROW_IF_FAILED(jumplistItems->AddObject(shLink.get()));
    }

    iconCache.PruneUnused();
}

// Method Description:
// - Creates a ShellLink object. Each item in a jumplist is a ShellLink, which is sort of
//   like a shortcut. It requires the path to the application (wt.exe), the arguments to pass,
//   and the path to the icon for the jumplist item. The path to the application isn't passed
//   into this function, as we'll determine it with GetWtExePath
// Arguments:
// - name: The name of the item displayed in the jumplist.
// - path: The path to the icon for the jumplist item.
// - args: The arguments to pass along with wt.exe
// - shLink: The shell link object to return.
// Return Value:
// - S_OK or HRESULT failure code.
winrt::com_ptr<IShellLinkW> Jumplist::_createShellLink(const std::wstring_view name, const std::wstring_view path, const std::wstring_view args)
{
    auto sh = winrt::create_instance<IShellLinkW>(CLSID_ShellLink, CLSCTX_ALL);

    const auto module{ GetWtExePath() };
    THROW_IF_FAILED(sh->SetPath(module.data()));
    THROW_IF_FAILED(sh->SetArguments(args.data()));
    auto propStore{ sh.as<IPropertyStore>() };

    PROPVARIANT titleProp;
    titleProp.vt = VT_LPWSTR;
    titleProp.pwszVal = const_cast<wchar_t*>(name.data());

    // Check for a comma in the path. If we find one we have an indirect icon. Parse the path into a file path and index/id.
    auto commaPosition = path.find(L",");
    if (commaPosition != std::wstring_view::npos)
    {
        const std::wstring iconPath{ path.substr(0, commaPosition) };

        // We dont want the comma included so add 1 to its position
        if (const auto iconIndex = til::parse_signed<int>(path.substr(commaPosition + 1)))
        {
            THROW_IF_FAILED(sh->SetIconLocation(iconPath.data(), *iconIndex));
        }
    }
    else if (til::ends_with(path, L"exe") || til::ends_with(path, L"dll") || til::ends_with(path, L"ico"))
    {
        // A binary or an .ico, but no index/id. Default to 0. (.ico is what
        // JumplistIconCache hands back for a rasterized glyph -- SetIconLocation
        // reads it directly, so it does not need the logo-URI path below.)
        THROW_IF_FAILED(sh->SetIconLocation(path.data(), 0));
    }
    else if (::Microsoft::Console::Utils::IsLikelyToBeEmojiOrSymbolIcon(path))
    {
        // Rasterizing this glyph failed upstream in _updateProfiles. Neither
        // branch above applies and DestListLogoUri would silently draw nothing,
        // so use our own icon: a Terminal logo beats an empty square.
        THROW_IF_FAILED(sh->SetIconLocation(module.data(), 0));
    }
    else
    {
        PROPVARIANT iconProp;
        iconProp.vt = VT_LPWSTR;
        iconProp.pwszVal = const_cast<wchar_t*>(path.data());

        THROW_IF_FAILED(propStore->SetValue(PKEY_AppUserModel_DestListLogoUri, iconProp));
    }

    THROW_IF_FAILED(propStore->SetValue(PKEY_Title, titleProp));
    THROW_IF_FAILED(propStore->Commit());

    return sh;
}
