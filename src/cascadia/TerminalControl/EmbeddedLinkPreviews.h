// Embedded links use the same rule resolver and provider as buffer links.
#pragma once
#include "HyperlinkRules.h"
#include "../../inc/LintelPaths.h"
#include "../../inc/LintelPathPatterns.h"
#include <winrt/Windows.UI.Xaml.Controls.Primitives.h>
#include <winrt/Windows.UI.Xaml.Documents.h>
#include <winrt/Windows.UI.Xaml.Input.h>
#include <winrt/Windows.System.h>

namespace winrt::Microsoft::Terminal::Control::implementation
{
    namespace Embedded
    {
        namespace X = winrt::Windows::UI::Xaml;
        namespace D = X::Documents;
        namespace C = X::Controls;
        inline thread_local int activePopups = 0;
        inline bool IsFileText(std::wstring_view text) { return Lintel::ClassifyPath(text) != Lintel::PathKind::None || til::starts_with_insensitive_ascii(text, L"file://"); }

        struct Hover : std::enable_shared_from_this<Hover>
        {
            Control::IHyperlinkPreviewProvider provider{ nullptr };
            Control::IControlSettings settings{ nullptr };
            winrt::weak_ref<X::FrameworkElement> owner;
            C::Primitives::Popup popup;
            X::DispatcherTimer showTimer, hideTimer;
            winrt::hstring current;
            winrt::Windows::Foundation::Point point{};
            int32_t depth{};
            bool inside{}, counted{};

            void Close()
            {
                showTimer.Stop(); hideTimer.Stop();
                popup.IsOpen(false);
                popup.Child(nullptr);
                if (counted) { --activePopups; counted = false; }
                current = {};
                inside = false;
            }
            ~Hover() { try { Close(); } catch (...) {} }

            void Leave()
            {
                showTimer.Stop();
                if (inside) return;
                const auto effective = ResolveHyperlinkRules(settings, std::wstring_view{ current }, IsFileText(std::wstring_view{ current }));
                hideTimer.Interval(std::chrono::milliseconds{ std::max(150, effective.hideDelay) });
                hideTimer.Start();
            }

            void Enter(const winrt::hstring& text, winrt::Windows::Foundation::Point at)
            {
                hideTimer.Stop();
                if (current == text && (popup.IsOpen() || showTimer.IsEnabled())) return;
                Close(); current = text; point = at;
                const auto effective = ResolveHyperlinkRules(settings, std::wstring_view{ text }, IsFileText(std::wstring_view{ text }));
                showTimer.Interval(std::chrono::milliseconds{ std::max(1, effective.showDelay) });
                showTimer.Start();
            }

            void Show()
            {
                showTimer.Stop();
                const auto root = owner.get();
                if (!root || !root.XamlRoot() || current.empty()) return;
                const auto effective = ResolveHyperlinkRules(settings, std::wstring_view{ current }, IsFileText(std::wstring_view{ current }));
                const auto size = root.XamlRoot().Size();
                const double width = std::min(effective.maxWidth > 0 ? static_cast<double>(effective.maxWidth) : 640.0, std::max(1.0, static_cast<double>(size.Width) - 16));
                const double height = std::min(settings.HyperlinkTooltipMaxHeight() > 0 ? static_cast<double>(settings.HyperlinkTooltipMaxHeight()) : static_cast<double>(size.Height), std::max(1.0, static_cast<double>(size.Height) - 16));
                C::Border border;
                border.Width(width); border.MaxHeight(height);
                border.CornerRadius(X::CornerRadius{ 8 });
                border.BorderThickness(X::Thickness{ 1 });
                border.RequestedTheme(root.ActualTheme());
                border.Background(X::Media::SolidColorBrush{ root.ActualTheme() == X::ElementTheme::Dark ? winrt::Windows::UI::Color{ 255, 40, 40, 40 } : winrt::Windows::UI::Color{ 255, 249, 249, 249 } });
                border.BorderBrush(X::Media::SolidColorBrush{ winrt::Windows::UI::Color{ 100, 128, 128, 128 } });
                const auto view = provider.CreatePreviewView(current, settings, depth + 1);
                view.MaxHeight(height);
                border.Child(view);
                const auto weak = weak_from_this();
                border.PointerEntered([weak](auto&&, auto&&) { if (const auto self = weak.lock()) { self->inside = true; self->hideTimer.Stop(); } });
                border.PointerExited([weak](auto&&, auto&&) { if (const auto self = weak.lock()) { self->inside = false; self->Leave(); } });
                border.KeyDown([weak](auto&&, const X::Input::KeyRoutedEventArgs& args) {
                    if (args.Key() == winrt::Windows::System::VirtualKey::Escape) if (const auto self = weak.lock()) { self->Close(); args.Handled(true); }
                });
                popup.XamlRoot(root.XamlRoot());
                popup.Child(border);
                popup.HorizontalOffset(std::clamp(static_cast<double>(point.X), 8.0, std::max(8.0, size.Width - width - 8)));
                popup.VerticalOffset(std::clamp(static_cast<double>(point.Y) + 16, 8.0, std::max(8.0, size.Height - height - 8)));
                popup.IsOpen(true);
                ++activePopups; counted = true;
            }
        };

        inline std::shared_ptr<Hover> MakeHover(const X::FrameworkElement& root, const Control::IHyperlinkPreviewProvider& provider, const Control::IControlSettings& settings, int32_t depth)
        {
            const auto state = std::make_shared<Hover>();
            state->owner = winrt::make_weak(root); state->provider = provider; state->settings = settings; state->depth = depth;
            const std::weak_ptr<Hover> weak = state;
            state->showTimer.Tick([weak](auto&&, auto&&) { try { if (const auto self = weak.lock()) self->Show(); } CATCH_LOG(); });
            state->hideTimer.Tick([weak](auto&&, auto&&) { if (const auto self = weak.lock(); self && !self->inside && activePopups <= self->depth + 1) self->Close(); });
            root.Unloaded([state](auto&&, auto&&) { state->Close(); });
            return state;
        }

        struct Match { int32_t start, end; };
        inline std::vector<Match> Matches(std::wstring_view text, const Control::IControlSettings& settings)
        {
            std::vector<Match> matches;
            const auto scan = [&](std::wstring_view pattern) {
                UErrorCode error = U_ZERO_ERROR;
                const auto re = til::ICU::CreateRegex(pattern, UREGEX_CASE_INSENSITIVE, &error);
                if (!re || U_FAILURE(error)) return;
                uregex_setText(re.get(), reinterpret_cast<const UChar*>(text.data()), static_cast<int32_t>(std::min(text.size(), size_t{ 20000 })), &error);
                while (matches.size() < 64 && uregex_findNext(re.get(), &error) && U_SUCCESS(error))
                {
                    const auto start = uregex_start(re.get(), 0, &error), end = uregex_end(re.get(), 0, &error);
                    if (end > start) matches.push_back({ start, end });
                }
            };
            scan(LR"(\b(?:https?|ftp|file)://[^\s<>"'`]+)");
            scan(Lintel::windowsPathPattern); scan(Lintel::posixPathPattern);
            if (const auto core = settings.try_as<winrt::Microsoft::Terminal::Core::ICoreSettings>())
            {
                if (const auto patterns = core.TextPatterns()) for (const auto& pattern : patterns) scan(std::wstring_view{ pattern });
            }
            std::sort(matches.begin(), matches.end(), [](const auto& a, const auto& b) { return a.start != b.start ? a.start < b.start : a.end > b.end; });
            return matches;
        }

        using Links = std::vector<std::pair<winrt::weak_ref<D::Hyperlink>, winrt::hstring>>;
        inline void Linkify(const D::InlineCollection& inlines, Links& links, const Control::IHyperlinkPreviewProvider& provider, const Control::IControlSettings& settings)
        {
            std::vector<D::Inline> output;
            for (const auto& item : inlines)
            {
                if (const auto link = item.try_as<D::Hyperlink>())
                {
                    if (link.NavigateUri())
                    {
                        const auto target = link.NavigateUri().AbsoluteUri();
                        links.emplace_back(winrt::make_weak(link), target);
                        link.NavigateUri(nullptr);
                        link.Click([provider, settings, target](auto&&, auto&&) { provider.InvokeLinkAction(target, settings, L"open"); });
                    }
                    output.push_back(item);
                }
                else if (const auto span = item.try_as<D::Span>()) { Linkify(span.Inlines(), links, provider, settings); output.push_back(item); }
                else if (const auto run = item.try_as<D::Run>())
                {
                    const std::wstring text{ run.Text() };
                    int32_t at = 0;
                    const auto makeRun = [&](int32_t start, int32_t end) {
                        D::Run part;
                        part.Text(winrt::hstring{ text.substr(start, end - start) });
                        part.FontWeight(run.FontWeight()); part.FontStyle(run.FontStyle()); part.FontFamily(run.FontFamily()); part.FontSize(run.FontSize());
                        return part;
                    };
                    for (const auto& match : Matches(text, settings))
                    {
                        if (match.start < at) continue;
                        if (match.start > at) output.push_back(makeRun(at, match.start));
                        const winrt::hstring value{ text.substr(match.start, match.end - match.start) };
                        D::Hyperlink link;
                        link.Click([provider, settings, value](auto&&, auto&&) { provider.InvokeLinkAction(value, settings, L"open"); });
                        link.Inlines().Append(makeRun(match.start, match.end));
                        links.emplace_back(winrt::make_weak(link), value); output.push_back(link); at = match.end;
                    }
                    if (at == 0) output.push_back(item);
                    else if (at < static_cast<int32_t>(text.size())) output.push_back(makeRun(at, static_cast<int32_t>(text.size())));
                }
                else output.push_back(item);
            }
            inlines.Clear();
            for (const auto& item : output) inlines.Append(item);
        }
    }

    bool HyperlinkPreviewHelpers::HasNestedPreview() { return Embedded::activePopups > 0; }

    void HyperlinkPreviewHelpers::AttachLinkTooltips(const Windows::UI::Xaml::FrameworkElement& root, const Control::IHyperlinkPreviewProvider& provider, const Control::IControlSettings& settings, bool compact, int32_t depth)
    {
        if (!root || !provider || !settings) return;
        if (const auto marker = root.Tag().try_as<hstring>(); marker && *marker == L"lintel-embedded-links") return;
        root.Tag(box_value(L"lintel-embedded-links"));
        const auto hoverEnabled = depth < 4 && (!compact || settings.HyperlinkTooltipNested());
        using namespace Embedded;
        if (const auto rich = root.try_as<C::RichTextBlock>())
        {
            Links links;
            for (const auto& block : rich.Blocks()) if (const auto paragraph = block.try_as<D::Paragraph>()) Linkify(paragraph.Inlines(), links, provider, settings);
            if (hoverEnabled)
            {
            for (const auto& [ref, target] : links) if (const auto link = ref.get()) C::ToolTipService::SetToolTip(link, nullptr);
            const auto state = MakeHover(root, provider, settings, depth);
            const auto weak = winrt::make_weak(rich);
            rich.PointerMoved([state, weak, links](auto&&, const X::Input::PointerRoutedEventArgs& args) {
                try
                {
                    const auto text = weak.get(); if (!text) return;
                    const auto point = args.GetCurrentPoint(text).Position();
                    const auto position = text.GetPositionFromPoint(point); if (!position) { state->Leave(); return; }
                    for (const auto& [ref, target] : links) if (const auto link = ref.get())
                    {
                        if (position.Offset() >= link.ContentStart().Offset() && position.Offset() < link.ContentEnd().Offset())
                        {
                            state->Enter(target, text.TransformToVisual(nullptr).TransformPoint(point)); return;
                        }
                    }
                    state->Leave();
                } CATCH_LOG();
            });
            rich.PointerExited([state](auto&&, auto&&) { state->Leave(); });
            }
        }
        else if (const auto link = root.try_as<C::HyperlinkButton>(); link && link.NavigateUri())
        {
            const auto target = link.NavigateUri().AbsoluteUri();
            link.NavigateUri(nullptr);
            link.Click([provider, settings, target](auto&&, auto&&) { provider.InvokeLinkAction(target, settings, L"open"); });
            if (hoverEnabled)
            {
                C::ToolTipService::SetToolTip(link, nullptr);
                const auto state = MakeHover(root, provider, settings, depth);
                const auto weak = winrt::make_weak(root);
                link.PointerEntered([state, weak, target](auto&&, auto&&) {
                    try { if (const auto element = weak.get()) state->Enter(target, element.TransformToVisual(nullptr).TransformPoint({ 0, static_cast<float>(element.ActualHeight()) })); } CATCH_LOG();
                });
                link.PointerExited([state](auto&&, auto&&) { state->Leave(); });
            }
        }
        root.Loaded([weak = winrt::make_weak(root), provider, settings, compact, depth](auto&&, auto&&) {
            try
            {
                if (const auto element = weak.get()) for (int32_t i = 0, count = X::Media::VisualTreeHelper::GetChildrenCount(element); i < count; ++i)
                    if (const auto child = X::Media::VisualTreeHelper::GetChild(element, i).try_as<X::FrameworkElement>()) HyperlinkPreviewHelpers::AttachLinkTooltips(child, provider, settings, compact, depth);
            } CATCH_LOG();
        });
        // Includes RichTextBlocks embedded in table cells, callouts and code surfaces.
        for (int32_t i = 0, count = X::Media::VisualTreeHelper::GetChildrenCount(root); i < count; ++i)
        {
            if (const auto child = X::Media::VisualTreeHelper::GetChild(root, i).try_as<X::FrameworkElement>()) AttachLinkTooltips(child, provider, settings, compact, depth);
        }
    }
}
