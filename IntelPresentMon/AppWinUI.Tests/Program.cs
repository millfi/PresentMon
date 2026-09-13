// Copyright (C) 2026 Intel Corporation
// SPDX-License-Identifier: MIT
using Microsoft.UI.Composition.SystemBackdrops;
using Microsoft.UI.Dispatching;
using Microsoft.UI.Windowing;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using PresentMon.UI.Views;

internal static class Program
{
    [STAThread]
    private static int Main()
    {
        WinRT.ComWrappersSupport.InitializeComWrappers();
        Application.Start(_ =>
        {
            SynchronizationContext.SetSynchronizationContext(
                new DispatcherQueueSynchronizationContext(DispatcherQueue.GetForCurrentThread()));
            new BackdropTests();
        });
        return Environment.ExitCode;
    }
}

internal sealed class BackdropTests : Application
{
    private Window? window;
    private Exception? unhandledException;

    public BackdropTests()
    {
        UnhandledException += (_, args) =>
        {
            unhandledException ??= args.Exception;
            args.Handled = true;
        };
    }

    protected override void OnLaunched(LaunchActivatedEventArgs args)
    {
        if (!DesktopAcrylicController.IsSupported())
        {
            Console.Error.WriteLine("Desktop acrylic support is required for these regression tests.");
            Environment.ExitCode = 1;
            Exit();
            return;
        }

        var root = new Grid { RequestedTheme = ElementTheme.Dark };
        window = new Window
        {
            Title = "PresentMon backdrop regression tests",
            Content = root,
            SystemBackdrop = new ThinAcrylicBackdrop(),
        };
        root.Loaded += async (_, _) => await RunAsync(root);
        window.Activate();
    }

    private async Task RunAsync(Grid root)
    {
        try
        {
            // Exercise real XAML notifications after transient WinRT wrappers can be collected.
            for (var pass = 0; pass < 3; ++pass)
            {
                foreach (var theme in new[] { ElementTheme.Light, ElementTheme.Dark, ElementTheme.Default,
                    ElementTheme.Light, ElementTheme.Dark })
                {
                    GC.Collect();
                    GC.WaitForPendingFinalizers();
                    GC.Collect();
                    root.RequestedTheme = theme;
                    await Task.Delay(100);
                    ThrowIfUnhandled();
                    var expected = theme == ElementTheme.Default
                        ? (RequestedTheme == ApplicationTheme.Light ? ElementTheme.Light : ElementTheme.Dark)
                        : theme;
                    if (root.ActualTheme != expected)
                        throw new InvalidOperationException($"Expected {expected}, got {root.ActualTheme}.");
                }

                var presenter = (OverlappedPresenter)window!.AppWindow.Presenter;
                presenter.Minimize();
                await Task.Delay(100);
                presenter.Restore();
                window.Activate();
                await Task.Delay(100);
                ThrowIfUnhandled();

                window.SystemBackdrop = null;
                window.SystemBackdrop = new ThinAcrylicBackdrop();
                await Task.Delay(100);
                ThrowIfUnhandled();
            }

            window!.Close();
            window = null;
            ThrowIfUnhandled();
            Console.WriteLine("PASS: 15 theme changes with garbage collection, minimize/restore, backdrop reconnect, and close.");
        }
        catch (Exception error)
        {
            Environment.ExitCode = 1;
            Console.Error.WriteLine(error);
        }
        finally
        {
            window?.Close();
            Exit();
        }
    }

    private void ThrowIfUnhandled()
    {
        if (unhandledException is not null)
            throw new InvalidOperationException("Unhandled backdrop notification failed.", unhandledException);
    }
}
