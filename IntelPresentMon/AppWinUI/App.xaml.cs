// Copyright (C) 2026 Intel Corporation
// SPDX-License-Identifier: MIT
using System;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Threading;
using Microsoft.UI.Xaml;
using PresentMon.UI.Services;

namespace PresentMon.UI;

public partial class App : Application
{
    private Window? _window;
    private Mutex? _instanceMutex;
    private string? _logDirectory;

    public App()
    {
        UnhandledException += OnUnhandledException;
        InitializeComponent();
    }

    protected override void OnLaunched(LaunchActivatedEventArgs args)
    {
        try
        {
            var options = StartupOptions.Parse(Environment.GetCommandLineArgs().Skip(1).ToArray());
            _logDirectory = options.LogDirectory;
            var mutex = new Mutex(true, @"Local\IntelPresentMon." + options.MutexSuffix, out var createdNew);
            if (!createdNew)
            {
                mutex.Dispose();
                Environment.ExitCode = 2;
                Exit();
                return;
            }

            _instanceMutex = mutex;
            _window = new MainWindow(options);
            _window.Closed += OnWindowClosed;
            _window.Activate();
        }
        catch (Exception exception)
        {
            LogException("Startup failure", exception);
            ReleaseInstanceMutex();
            Environment.ExitCode = 1;
            MessageBoxW(0, "PresentMon could not start.\n\n" + exception.Message,
                "PresentMon Startup Error", 0x00000010);
            Exit();
        }
    }

    private void OnUnhandledException(object sender, Microsoft.UI.Xaml.UnhandledExceptionEventArgs args)
    {
        LogException("Unhandled UI exception", args.Exception);
    }

    private void LogException(string context, Exception exception)
    {
        var message = $"{DateTimeOffset.Now:O} {context}:{Environment.NewLine}{exception}{Environment.NewLine}";
        Debug.WriteLine(message);
        if (string.IsNullOrWhiteSpace(_logDirectory)) return;
        try
        {
            Directory.CreateDirectory(_logDirectory);
            File.AppendAllText(Path.Combine(_logDirectory, "PresentMonUI.log"), message);
        }
        catch (Exception loggingError)
        {
            Debug.WriteLine($"Unable to write the PresentMon error log: {loggingError}");
        }
    }

    private void OnWindowClosed(object sender, WindowEventArgs args)
    {
        _window = null;
        ReleaseInstanceMutex();
    }

    private void ReleaseInstanceMutex()
    {
        var mutex = _instanceMutex;
        _instanceMutex = null;
        if (mutex is null)
        {
            return;
        }

        try
        {
            mutex.ReleaseMutex();
        }
        finally
        {
            mutex.Dispose();
        }
    }

    [DllImport("user32.dll", CharSet = CharSet.Unicode, ExactSpelling = true)]
    private static extern int MessageBoxW(nint owner, string text, string caption, uint type);
}
