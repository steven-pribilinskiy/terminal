// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.
#pragma once
#include <string_view>

namespace Microsoft::Terminal::PreviewPresentation
{
    // Use manifest identity, never user-facing labels such as Jira's "Parent".
    constexpr bool IsCommitGroup(std::wstring_view integration, std::wstring_view group) noexcept
    {
        return integration == L"github" && group == L"commit";
    }

    constexpr bool IsZeroCount(std::wstring_view value) noexcept
    {
        return value == L"0";
    }
}
