// Copyright (C) 2026 Intel Corporation
// SPDX-License-Identifier: MIT
using System;
using System.IO;

namespace PresentMon.UI.Services;

public sealed record StartupOptions
{
    public const string DefaultMutexSuffix = "UiBrowserProcess";

    public string PipeName { get; init; } = string.Empty;
    public bool FilesWorking { get; init; }
    public string MutexSuffix { get; init; } = DefaultMutexSuffix;
    public string DataDirectory { get; init; } = string.Empty;
    public string AppDataDirectory { get; init; } = string.Empty;
    public string InstallDirectory { get; init; } = string.Empty;
    public string LogDirectory { get; init; } = string.Empty;
    public bool EnableDevOptions { get; init; }

    public static StartupOptions Parse(string[] args)
    {
        ArgumentNullException.ThrowIfNull(args);
        var pipeName = string.Empty;
        var filesWorking = false;
        var mutexSuffix = DefaultMutexSuffix;
        var logDirectory = string.Empty;
        var dataDirectory = string.Empty;
        var enableDevOptions = false;

        for (var index = 0; index < args.Length; index++)
        {
            var argument = args[index];
            var separator = argument.IndexOf('=');
            var name = separator < 0 ? argument : argument[..separator];
            string ReadValue()
            {
                var value = separator >= 0 ? argument[(separator + 1)..]
                    : index + 1 < args.Length && !args[index + 1].StartsWith("--", StringComparison.Ordinal)
                        ? args[++index] : string.Empty;
                if (string.IsNullOrWhiteSpace(value))
                {
                    throw new ArgumentException($"The {name} option requires a value.", nameof(args));
                }
                return value;
            }

            bool ReadFlag()
            {
                if (separator < 0)
                {
                    return true;
                }
                return bool.TryParse(argument[(separator + 1)..], out var value)
                    ? value : throw new ArgumentException($"The {name} option requires true or false.", nameof(args));
            }

            switch (name)
            {
                case "--p2c-act-name":
                    pipeName = ReadValue();
                    break;
                case "--p2c-files-working":
                    filesWorking = ReadFlag();
                    break;
                case "--p2c-ui-mutex-name":
                    mutexSuffix = ReadValue();
                    break;
                case "--p2c-log-folder":
                    logDirectory = ReadValue();
                    break;
                case "--p2c-enable-ui-dev-options":
                    enableDevOptions = ReadFlag();
                    break;
                case "--data-directory":
                    dataDirectory = ReadValue();
                    break;
                // The kernel also forwards legacy CEF and logging switches.
                // Unrecognized arguments are intentionally ignored, as in CEF.
            }
        }

        string appDataDirectory;
        if (!string.IsNullOrEmpty(dataDirectory))
        {
            dataDirectory = Path.GetFullPath(dataDirectory);
            appDataDirectory = dataDirectory;
        }
        else if (filesWorking)
        {
            dataDirectory = Environment.CurrentDirectory;
            appDataDirectory = dataDirectory;
        }
        else
        {
            dataDirectory = Path.Combine(GetKnownFolder(Environment.SpecialFolder.MyDocuments), "PresentMon");
            appDataDirectory = Path.Combine(GetKnownFolder(Environment.SpecialFolder.LocalApplicationData), "Intel", "PresentMon");
        }

        var executableDirectory = Path.TrimEndingDirectorySeparator(Path.GetFullPath(AppContext.BaseDirectory));
        var installDirectory = string.Equals(Path.GetFileName(executableDirectory), "ui", StringComparison.OrdinalIgnoreCase)
            ? Directory.GetParent(executableDirectory)!.FullName : executableDirectory;
        return new StartupOptions
        {
            PipeName = pipeName,
            FilesWorking = filesWorking,
            MutexSuffix = mutexSuffix,
            DataDirectory = dataDirectory,
            AppDataDirectory = appDataDirectory,
            InstallDirectory = installDirectory,
            LogDirectory = string.IsNullOrWhiteSpace(logDirectory)
                ? Path.Combine(appDataDirectory, "logs") : Path.GetFullPath(logDirectory),
            EnableDevOptions = enableDevOptions,
        };
    }

    private static string GetKnownFolder(Environment.SpecialFolder folder)
    {
        var path = Environment.GetFolderPath(folder, Environment.SpecialFolderOption.DoNotVerify);
        if (string.IsNullOrWhiteSpace(path))
        {
            throw new DirectoryNotFoundException($"Windows did not provide the {folder} folder.");
        }
        return path;
    }
}
