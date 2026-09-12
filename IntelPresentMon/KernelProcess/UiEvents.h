#pragma once
#include <Interprocess/source/act/AsyncAction.h>
#include <cstdint>

namespace kproc::ui
{
    // The managed control panel receives these events over the action channel.
    struct TargetLostAction : pmon::ipc::act::EventDeclaration
    {
        static constexpr const char* Identifier = "TargetLostAction";
        struct Params
        {
            uint32_t pid;
            template<class Archive> void serialize(Archive& ar) { ar(pid); }
        };
    };

    struct OverlayDiedAction : pmon::ipc::act::EventDeclaration
    {
        static constexpr const char* Identifier = "OverlayDiedAction";
        struct Params
        {
            template<class Archive> void serialize(Archive&) {}
        };
    };

    struct PresentmonInitFailedAction : pmon::ipc::act::EventDeclaration
    {
        static constexpr const char* Identifier = "PresentmonInitFailedAction";
        struct Params
        {
            template<class Archive> void serialize(Archive&) {}
        };
    };

    struct StalePidAction : pmon::ipc::act::EventDeclaration
    {
        static constexpr const char* Identifier = "StalePidAction";
        struct Params
        {
            template<class Archive> void serialize(Archive&) {}
        };
    };

    struct HotkeyFiredAction : pmon::ipc::act::EventDeclaration
    {
        static constexpr const char* Identifier = "HotkeyFiredAction";
        struct Params
        {
            int32_t actionId;
            template<class Archive> void serialize(Archive& ar) { ar(actionId); }
        };
    };
}

namespace pmon::ipc::act
{
    template<> struct ActionParamsTraits<kproc::ui::TargetLostAction::Params>
    {
        using Action = kproc::ui::TargetLostAction;
    };
    template<> struct ActionParamsTraits<kproc::ui::OverlayDiedAction::Params>
    {
        using Action = kproc::ui::OverlayDiedAction;
    };
    template<> struct ActionParamsTraits<kproc::ui::PresentmonInitFailedAction::Params>
    {
        using Action = kproc::ui::PresentmonInitFailedAction;
    };
    template<> struct ActionParamsTraits<kproc::ui::StalePidAction::Params>
    {
        using Action = kproc::ui::StalePidAction;
    };
    template<> struct ActionParamsTraits<kproc::ui::HotkeyFiredAction::Params>
    {
        using Action = kproc::ui::HotkeyFiredAction;
    };
}
