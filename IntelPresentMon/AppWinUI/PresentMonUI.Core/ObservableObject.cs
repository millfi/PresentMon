using System.Collections;
using System.Collections.Specialized;
using System.ComponentModel;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace PresentMon.UI.Core;

public abstract class ObservableObject : INotifyPropertyChanged
{
    public event PropertyChangedEventHandler? PropertyChanged;

    protected bool SetProperty<T>(ref T storage, T value, [CallerMemberName] string? name = null)
    {
        if (EqualityComparer<T>.Default.Equals(storage, value)) return false;
        storage = value;
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(name));
        return true;
    }

    // Unknown fields survive a load/save round trip through a newer frontend.
    [JsonExtensionData]
    public Dictionary<string, JsonElement>? AdditionalProperties { get; set; }
}

/// <summary>Observes property and collection edits throughout a model graph.</summary>
public sealed class ObjectChangeTracker : IDisposable
{
    private readonly object root;
    private readonly Action changed;
    private readonly List<Action> detach = [];
    private bool disposed;

    public ObjectChangeTracker(object root, Action changed)
    {
        this.root = root;
        this.changed = changed;
        Attach();
    }

    private void Attach()
    {
        var visited = new HashSet<object>(ReferenceEqualityComparer.Instance);
        void Visit(object? item)
        {
            if (item is null || item is string || item.GetType().IsValueType || !visited.Add(item)) return;
            if (item is INotifyPropertyChanged properties)
            {
                PropertyChangedEventHandler handler = (_, _) => OnChanged();
                properties.PropertyChanged += handler;
                detach.Add(() => properties.PropertyChanged -= handler);
            }
            if (item is INotifyCollectionChanged collection)
            {
                NotifyCollectionChangedEventHandler handler = (_, _) => OnChanged();
                collection.CollectionChanged += handler;
                detach.Add(() => collection.CollectionChanged -= handler);
            }
            if (item is IDictionary dictionary)
            {
                foreach (var child in dictionary.Values) Visit(child);
            }
            else if (item is IEnumerable enumerable)
            {
                foreach (var child in enumerable) Visit(child);
            }
            else if (item is ObservableObject || item is PreferenceFile || item is LoadoutFile)
            {
                foreach (var property in item.GetType().GetProperties(BindingFlags.Public | BindingFlags.Instance))
                    if (property.CanRead && property.GetIndexParameters().Length == 0) Visit(property.GetValue(item));
            }
        }
        Visit(root);
    }

    private void OnChanged()
    {
        if (disposed) return;
        Detach();
        Attach();
        changed();
    }

    private void Detach()
    {
        foreach (var action in detach) action();
        detach.Clear();
    }

    public void Dispose()
    {
        disposed = true;
        Detach();
    }
}
