// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "pch.h"

#include "../TerminalSettingsModel/ApplicationState.h"

using namespace Microsoft::Console;
using namespace WEX::Logging;
using namespace WEX::TestExecution;
using namespace WEX::Common;
using namespace winrt::Microsoft::Terminal::Settings::Model;

namespace SettingsModelUnitTests
{
    // Covers the workspace-persistence APIs added to ApplicationState:
    //   SaveWorkspace / RemoveWorkspace / RenameWorkspace / TakeWorkspace /
    //   AllPersistedWorkspaces.
    // All tests operate on a throw-away ApplicationState instance pointed at
    // a temp directory, so they don't touch the real user state.
    class ApplicationStateTests
    {
        TEST_CLASS(ApplicationStateTests);

        TEST_METHOD(SaveAndLookupWorkspace);
        TEST_METHOD(RemoveWorkspaceReturnsFalseWhenMissing);
        TEST_METHOD(RenameWorkspaceMigratesEntry);
        TEST_METHOD(RenameWorkspaceNoOpForEmptyOrEqualNames);
        TEST_METHOD(RenameWorkspaceNoOpForMissingEntry);
        TEST_METHOD(TakeWorkspaceRemovesAndReturns);
        TEST_METHOD(TakeWorkspaceReturnsNullWhenMissing);

        TEST_METHOD(SaveAndLookupWindowGeometry);
        TEST_METHOD(LookupWindowGeometryReturnsNullWhenMissing);
        TEST_METHOD(WindowGeometryIsKeyedByWindowName);
        TEST_METHOD(WindowGeometryPersistsAcrossInstances);

    private:
        static std::filesystem::path _tempRoot()
        {
            auto root = std::filesystem::temp_directory_path() / L"WT_ApplicationStateTests";
            std::error_code ec;
            std::filesystem::create_directories(root, ec);
            // Best-effort clean of any leftover state.json from a prior run so
            // tests see an empty starting point.
            std::filesystem::remove(root / L"state.json", ec);
            std::filesystem::remove(root / L"elevated-state.json", ec);
            return root;
        }

        static winrt::com_ptr<implementation::ApplicationState> _make()
        {
            return winrt::make_self<implementation::ApplicationState>(_tempRoot());
        }

        static WindowLayout _makeLayout()
        {
            WindowLayout layout;
            layout.TabLayout(winrt::single_threaded_vector<ActionAndArgs>());
            return layout;
        }

        static WindowGeometry _makeGeometry(int32_t left = 120, int32_t top = 340)
        {
            WindowGeometry geometry;
            geometry.Left(left);
            geometry.Top(top);
            geometry.Width(1280);
            geometry.Height(800);
            geometry.Dpi(144);
            geometry.LaunchMode(LaunchMode::MaximizedMode);
            return geometry;
        }
    };

    void ApplicationStateTests::SaveAndLookupWorkspace()
    {
        auto state = _make();
        const auto layout = _makeLayout();
        state->SaveWorkspace(L"win1", layout);

        const auto all = state->AllPersistedWorkspaces();
        VERIFY_IS_NOT_NULL(all);
        VERIFY_IS_TRUE(all.HasKey(L"win1"));
    }

    void ApplicationStateTests::RemoveWorkspaceReturnsFalseWhenMissing()
    {
        auto state = _make();
        VERIFY_IS_FALSE(state->RemoveWorkspace(L"does-not-exist"));

        state->SaveWorkspace(L"win1", _makeLayout());
        VERIFY_IS_TRUE(state->RemoveWorkspace(L"win1"));
        VERIFY_IS_FALSE(state->RemoveWorkspace(L"win1"));
    }

    void ApplicationStateTests::RenameWorkspaceMigratesEntry()
    {
        auto state = _make();
        state->SaveWorkspace(L"oldName", _makeLayout());

        VERIFY_IS_TRUE(state->RenameWorkspace(L"oldName", L"newName"));

        const auto all = state->AllPersistedWorkspaces();
        VERIFY_IS_NOT_NULL(all);
        VERIFY_IS_FALSE(all.HasKey(L"oldName"));
        VERIFY_IS_TRUE(all.HasKey(L"newName"));
    }

    void ApplicationStateTests::RenameWorkspaceNoOpForEmptyOrEqualNames()
    {
        auto state = _make();
        state->SaveWorkspace(L"win1", _makeLayout());

        VERIFY_IS_FALSE(state->RenameWorkspace(L"win1", L"win1"));
        VERIFY_IS_FALSE(state->RenameWorkspace(L"", L"win2"));

        // Renaming to an empty name removes the stale entry under the old name.
        VERIFY_IS_TRUE(state->RenameWorkspace(L"win1", L""));
        const auto all = state->AllPersistedWorkspaces();
        if (all)
        {
            VERIFY_IS_FALSE(all.HasKey(L"win1"));
            VERIFY_IS_FALSE(all.HasKey(L""));
        }

        // Calling again is now a no-op because the entry is gone.
        VERIFY_IS_FALSE(state->RenameWorkspace(L"win1", L""));
    }

    void ApplicationStateTests::RenameWorkspaceNoOpForMissingEntry()
    {
        auto state = _make();
        VERIFY_IS_FALSE(state->RenameWorkspace(L"missing", L"newName"));
    }

    void ApplicationStateTests::TakeWorkspaceRemovesAndReturns()
    {
        auto state = _make();
        state->SaveWorkspace(L"win1", _makeLayout());

        const auto taken = state->TakeWorkspace(L"win1");
        VERIFY_IS_NOT_NULL(taken);

        // Subsequent Take for the same name must return null — this is the
        // atomicity guarantee the startup path relies on.
        VERIFY_IS_NULL(state->TakeWorkspace(L"win1"));
    }

    void ApplicationStateTests::TakeWorkspaceReturnsNullWhenMissing()
    {
        auto state = _make();
        VERIFY_IS_NULL(state->TakeWorkspace(L"missing"));
    }

    // GH#12633: remembered window geometry.

    void ApplicationStateTests::SaveAndLookupWindowGeometry()
    {
        auto state = _make();
        state->SaveWindowGeometry(L"", _makeGeometry());

        const auto geometry = state->LookupWindowGeometry(L"");
        VERIFY_IS_NOT_NULL(geometry);
        VERIFY_ARE_EQUAL(120, geometry.Left());
        VERIFY_ARE_EQUAL(340, geometry.Top());
        VERIFY_ARE_EQUAL(1280, geometry.Width());
        VERIFY_ARE_EQUAL(800, geometry.Height());
        VERIFY_ARE_EQUAL(144u, geometry.Dpi());
        VERIFY_ARE_EQUAL(LaunchMode::MaximizedMode, geometry.LaunchMode());

        // Unlike TakeWorkspace, a lookup must not consume the entry — the same
        // geometry is reused every time a window opens under that name.
        VERIFY_IS_NOT_NULL(state->LookupWindowGeometry(L""));
    }

    void ApplicationStateTests::LookupWindowGeometryReturnsNullWhenMissing()
    {
        auto state = _make();
        VERIFY_IS_NULL(state->LookupWindowGeometry(L"never-seen"));
    }

    void ApplicationStateTests::WindowGeometryIsKeyedByWindowName()
    {
        auto state = _make();
        state->SaveWindowGeometry(L"", _makeGeometry(10, 20));
        state->SaveWindowGeometry(L"named", _makeGeometry(900, 500));

        VERIFY_ARE_EQUAL(10, state->LookupWindowGeometry(L"").Left());
        VERIFY_ARE_EQUAL(900, state->LookupWindowGeometry(L"named").Left());
    }

    void ApplicationStateTests::WindowGeometryPersistsAcrossInstances()
    {
        const auto root = _tempRoot();
        {
            auto state = winrt::make_self<implementation::ApplicationState>(root);
            state->SaveWindowGeometry(L"named", _makeGeometry(64, 96));
            state->Flush();
        }

        // A second instance over the same directory has to read the geometry
        // back out of state.json, which exercises the JSON conversion trait.
        auto reloaded = winrt::make_self<implementation::ApplicationState>(root);
        const auto geometry = reloaded->LookupWindowGeometry(L"named");
        VERIFY_IS_NOT_NULL(geometry);
        VERIFY_ARE_EQUAL(64, geometry.Left());
        VERIFY_ARE_EQUAL(96, geometry.Top());
        VERIFY_ARE_EQUAL(1280, geometry.Width());
        VERIFY_ARE_EQUAL(800, geometry.Height());
        VERIFY_ARE_EQUAL(144u, geometry.Dpi());
        VERIFY_ARE_EQUAL(LaunchMode::MaximizedMode, geometry.LaunchMode());
    }
}
