using PresentMon.UI.Services;

internal static class StartupOptionsTests
{
    public static int Run()
    {
        var workingDirectory = Path.GetFullPath(Environment.CurrentDirectory);
        var options = StartupOptions.Parse(
        [
            "--p2c-act-name", "presentmon-test-pipe",
            "--p2c-files-working",
            "--p2c-ui-mutex-name", "PresentMonTestUI",
            "--p2c-log-folder", workingDirectory,
            "--p2c-enable-ui-dev-options",
        ]);
        if (options.PipeName != "presentmon-test-pipe" || !options.FilesWorking
            || options.MutexSuffix != "PresentMonTestUI" || options.LogDirectory != workingDirectory
            || !options.EnableDevOptions || options.DataDirectory != workingDirectory)
            throw new InvalidOperationException("The native launch arguments were not parsed correctly.");

        foreach (var argument in new[] { "--disable-gpu", "--p2c-ui-url=about:blank", "--p2c-log-level=debug" })
        {
            try { StartupOptions.Parse([argument]); }
            catch (ArgumentException) { continue; }
            throw new InvalidOperationException("Unsupported UI launch arguments must be rejected.");
        }
        Console.WriteLine("PASS Native launch arguments work and obsolete UI switches are rejected");
        return 1;
    }
}
