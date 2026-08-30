// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.
//
// Local copies of profile icons that are given as http(s) URLs.
//
// Fragments shipped inside other packages routinely point an icon at the web --
// Canonical's Ubuntu fragment uses
// https://assets.ubuntu.com/v1/49a1a858-favicon-32x32.png -- because a fragment
// cannot reference an asset inside its own package (microsoft/terminal#10359).
// Nothing local can draw a URL, so such an icon used to decay to a
// fragment-relative filename, fail to exist, and fall back to the profile's
// commandline. For Ubuntu that is ubuntu.exe, a zero-byte WindowsApps alias
// stub with no icon at all, which is why the profile drew a blank square.
//
// So keep a copy. This is what microsoft/terminal#10552 has been asking for
// since 2021 -- "we could actually download the image locally and set the path
// for the jumplist entry to that file", and DHowett's "we should cache local
// *and* remote paths, honestly".
//
// Resolution never blocks on the network. A cached file is used immediately; a
// missing one starts a background download and lets the caller fall back as
// before, so the real icon appears on the next settings reload. Settings load
// is on the startup path and must not wait on a web request.

#pragma once

#include <optional>
#include <string>

// Namespace matches FileUtils.h -- this lives alongside GetBaseSettingsPath().
namespace winrt::Microsoft::Terminal::Settings::Model
{
    // The local file already downloaded for this URL, if there is one.
    std::optional<std::wstring> TryGetCachedRemoteIcon(const std::wstring_view url);

    // Starts a download for `url` unless one is already cached or in flight.
    // Returns immediately; the result is picked up by a later resolution.
    void RequestRemoteIcon(const std::wstring_view url);
}
