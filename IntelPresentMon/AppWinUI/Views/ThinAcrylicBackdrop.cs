// Copyright (C) 2026 Intel Corporation
// SPDX-License-Identifier: MIT
using Microsoft.UI.Composition;
using Microsoft.UI.Composition.SystemBackdrops;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Media;

namespace PresentMon.UI.Views;

public sealed class ThinAcrylicBackdrop : SystemBackdrop
{
    private DesktopAcrylicController? controller;

    protected override void OnTargetConnected(ICompositionSupportsSystemBackdrop connectedTarget, XamlRoot xamlRoot)
    {
        base.OnTargetConnected(connectedTarget, xamlRoot);
        if (!DesktopAcrylicController.IsSupported()) return;
        if (controller is not null)
            throw new InvalidOperationException("Create a separate acrylic backdrop for each window.");

        controller = new DesktopAcrylicController { Kind = DesktopAcrylicKind.Thin };
        // XAML keeps the default configuration in sync with theme, activation, and accessibility.
        controller.SetSystemBackdropConfiguration(GetDefaultSystemBackdropConfiguration(connectedTarget, xamlRoot));
        controller.AddSystemBackdropTarget(connectedTarget);
    }

    protected override void OnDefaultSystemBackdropConfigurationChanged(
        ICompositionSupportsSystemBackdrop target, XamlRoot xamlRoot)
    {
        // XAML has already updated the configuration observed by the controller.
        // Do not forward to the SDK base callback: after GC its weak target can be
        // null, and the native ABI rejects it with E_INVALIDARG.
    }

    protected override void OnTargetDisconnected(ICompositionSupportsSystemBackdrop disconnectedTarget)
    {
        if (controller is not null)
        {
            controller.RemoveSystemBackdropTarget(disconnectedTarget);
            controller.Dispose();
            controller = null;
        }
        base.OnTargetDisconnected(disconnectedTarget);
    }
}
