using System.Text;
using System.Text.Json;

namespace PresentMon.UI.Core;

/// <summary>Reads supported preference and loadout files, and saves them atomically.</summary>
public sealed class ConfigurationStore
{
    private readonly object saveLock = new();
    private readonly HashSet<string> failedLoads = new(StringComparer.OrdinalIgnoreCase);

    public ConfigurationStore(string dataDirectory, string presetDirectory)
    {
        DataDirectory = Path.GetFullPath(dataDirectory);
        PresetDirectory = Path.GetFullPath(presetDirectory);
    }

    public string DataDirectory { get; }
    public string PresetDirectory { get; }
    public string PreferencesPath => Path.Combine(DataDirectory, "preferences.json");
    public string CustomPath => Path.Combine(DataDirectory, "Loadouts", "custom-auto.json");
    public string? LastWarning { get; private set; }

    public PreferenceFile LoadPreferences(IntrospectionData intro)
    {
        LastWarning = null;
        if (!File.Exists(PreferencesPath)) return Defaults(intro);
        try { return PreferenceDocument.Parse(File.ReadAllText(PreferencesPath), intro); }
        catch (Exception exception) when (IsLoadError(exception))
        {
            failedLoads.Add(PreferencesPath);
            LastWarning = $"Preferences could not be loaded: {exception.Message} The original file will be retained as a recovery copy when settings are saved.";
            return Defaults(intro);
        }
    }

    public void SavePreferences(PreferenceFile file) => AtomicWrite(PreferencesPath, PreferenceDocument.Serialize(file));

    public LoadoutFile LoadPreset(int slot, IntrospectionData intro, Preferences preferences)
    {
        if (slot is < 0 or > 3) throw new ArgumentOutOfRangeException(nameof(slot));
        return LoadLoadout(Path.Combine(PresetDirectory, $"preset-{slot}.json"), intro, preferences);
    }

    public LoadoutFile LoadCustom(IntrospectionData intro, Preferences preferences)
    {
        LastWarning = null;
        if (!File.Exists(CustomPath)) return new();
        try { return LoadLoadout(CustomPath, intro, preferences); }
        catch (Exception exception) when (IsLoadError(exception))
        {
            failedLoads.Add(CustomPath);
            LastWarning = $"The custom loadout could not be loaded: {exception.Message} The original file will be retained as a recovery copy when the loadout is saved.";
            return new();
        }
    }

    public void SaveCustom(LoadoutFile file) => SaveLoadout(CustomPath, file);

    public LoadoutFile LoadLoadout(string path, IntrospectionData intro, Preferences preferences)
    {
        var fullPath = Path.GetFullPath(path);
        try { return LoadoutDocument.Parse(File.ReadAllText(fullPath), intro, preferences); }
        catch (Exception exception) when (IsLoadError(exception))
        {
            if (File.Exists(fullPath)) failedLoads.Add(fullPath);
            throw;
        }
    }

    public void SaveLoadout(string path, LoadoutFile file) => AtomicWrite(Path.GetFullPath(path), LoadoutDocument.Serialize(file));

    public static string DefaultDataDirectory => Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.MyDocuments), "PresentMon");

    private static PreferenceFile Defaults(IntrospectionData intro) => new() { Preferences = Preferences.CreateDefault(intro) };
    private static bool IsLoadError(Exception error) => error is IOException or UnauthorizedAccessException or JsonException
        or ArgumentException or InvalidOperationException or NullReferenceException;

    private void AtomicWrite(string path, string content)
    {
        lock (saveLock)
        {
            Directory.CreateDirectory(Path.GetDirectoryName(path)!);
            var temporary = path + "." + Guid.NewGuid().ToString("N") + ".tmp";
            try
            {
                using (var stream = new FileStream(temporary, FileMode.CreateNew, FileAccess.Write, FileShare.None))
                {
                    using (var writer = new StreamWriter(stream, new UTF8Encoding(false), leaveOpen: true))
                    {
                        writer.Write(content);
                        writer.Flush();
                    }
                    stream.Flush(true);
                }
                if (File.Exists(path))
                {
                    if (failedLoads.Contains(path))
                    {
                        // Never replace the only copy of an unreadable or unsupported document.
                        var recovery = path + ".recovery-" + DateTime.UtcNow.ToString("yyyyMMdd-HHmmss") + "-" + Guid.NewGuid().ToString("N") + ".json";
                        File.Copy(path, recovery, false);
                        failedLoads.Remove(path);
                    }
                    File.Replace(temporary, path, path + ".bak");
                }
                else File.Move(temporary, path);
            }
            finally
            {
                if (File.Exists(temporary)) File.Delete(temporary);
            }
        }
    }
}
