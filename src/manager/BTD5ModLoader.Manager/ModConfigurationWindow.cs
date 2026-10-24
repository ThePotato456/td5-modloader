using System.Globalization;
using System.Text.Json;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;

namespace BTD5ModLoader.Manager;

internal sealed class ModConfigurationWindow : Window
{
    private readonly IReadOnlyDictionary<string, JsonElement> defaults;
    private readonly Dictionary<string, FrameworkElement> editors = new(StringComparer.Ordinal);
    private readonly TextBlock errorText = new() { Foreground = Brushes.IndianRed };

    public ModConfigurationWindow(
        string modName,
        IReadOnlyDictionary<string, JsonElement> configuration,
        IReadOnlyDictionary<string, JsonElement> defaults)
    {
        this.defaults = defaults;
        Configuration = configuration.ToDictionary(
            value => value.Key, value => value.Value.Clone(), StringComparer.Ordinal);
        Title = "Configure " + modName;
        Width = 520;
        Height = 520;
        MinWidth = 420;
        MinHeight = 320;
        WindowStartupLocation = WindowStartupLocation.CenterOwner;
        Background = new SolidColorBrush(Color.FromRgb(21, 29, 39));
        Foreground = Brushes.White;

        var root = new DockPanel { Margin = new Thickness(18) };
        var introduction = new TextBlock
        {
            Text = $"{modName} settings are stored only in the current profile.",
            Foreground = Brushes.LightGray,
            TextWrapping = TextWrapping.Wrap,
            Margin = new Thickness(0, 0, 0, 16)
        };
        DockPanel.SetDock(introduction, Dock.Top);
        root.Children.Add(introduction);
        var buttons = new StackPanel
        {
            Orientation = Orientation.Horizontal,
            HorizontalAlignment = HorizontalAlignment.Right,
            Margin = new Thickness(0, 14, 0, 0)
        };
        DockPanel.SetDock(buttons, Dock.Bottom);
        var reset = MakeButton("Reset all");
        reset.Click += (_, _) => Populate(defaults);
        var cancel = MakeButton("Cancel");
        cancel.Margin = new Thickness(8, 0, 0, 0);
        cancel.Click += (_, _) => DialogResult = false;
        var save = MakeButton("Save");
        save.Margin = new Thickness(8, 0, 0, 0);
        save.IsDefault = true;
        save.Click += Save_Click;
        buttons.Children.Add(reset);
        buttons.Children.Add(cancel);
        buttons.Children.Add(save);
        root.Children.Add(buttons);
        DockPanel.SetDock(errorText, Dock.Bottom);
        root.Children.Add(errorText);

        var form = new StackPanel();
        foreach (var (key, defaultValue) in defaults.OrderBy(value => value.Key, StringComparer.Ordinal))
        {
            var row = new Grid { Margin = new Thickness(0, 0, 0, 12) };
            row.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(0.42, GridUnitType.Star) });
            row.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(0.58, GridUnitType.Star) });
            row.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
            row.Children.Add(new TextBlock
            {
                Text = key,
                VerticalAlignment = VerticalAlignment.Center,
                TextWrapping = TextWrapping.Wrap,
                Margin = new Thickness(0, 0, 12, 0)
            });
            var value = configuration.TryGetValue(key, out var configured) ? configured : defaultValue;
            FrameworkElement editor = defaultValue.ValueKind switch
            {
                JsonValueKind.True or JsonValueKind.False => new CheckBox
                {
                    IsChecked = value.GetBoolean(),
                    VerticalAlignment = VerticalAlignment.Center
                },
                JsonValueKind.String or JsonValueKind.Number => new TextBox
                {
                    Text = defaultValue.ValueKind == JsonValueKind.String
                        ? value.GetString() ?? string.Empty
                        : value.GetRawText(),
                    Foreground = Brushes.White,
                    Background = new SolidColorBrush(Color.FromRgb(16, 23, 32))
                },
                _ => new TextBlock
                {
                    Text = "Unsupported setting type",
                    Foreground = Brushes.Goldenrod
                }
            };
            Grid.SetColumn(editor, 1);
            row.Children.Add(editor);
            editors[key] = editor;
            var resetSetting = MakeButton("Reset");
            resetSetting.Padding = new Thickness(9, 5, 9, 5);
            resetSetting.Margin = new Thickness(8, 0, 0, 0);
            resetSetting.ToolTip = $"Reset {key} to its package default.";
            resetSetting.Click += (_, _) => SetEditorValue(key, defaultValue);
            Grid.SetColumn(resetSetting, 2);
            row.Children.Add(resetSetting);
            form.Children.Add(row);
        }
        if (defaults.Count == 0)
        {
            form.Children.Add(new TextBlock
            {
                Text = "This mod does not define any configurable settings.",
                Foreground = Brushes.LightGray
            });
        }
        root.Children.Add(new ScrollViewer { Content = form, VerticalScrollBarVisibility = ScrollBarVisibility.Auto });
        Content = root;
    }

    public IReadOnlyDictionary<string, JsonElement> Configuration { get; private set; }

    private void Populate(IReadOnlyDictionary<string, JsonElement> values)
    {
        foreach (var (key, value) in values)
        {
            if (!editors.TryGetValue(key, out var editor))
            {
                continue;
            }
            SetEditorValue(key, value);
        }
        errorText.Text = string.Empty;
    }

    private void SetEditorValue(string key, JsonElement value)
    {
        if (!editors.TryGetValue(key, out var editor))
        {
            return;
        }
        if (editor is CheckBox checkBox)
        {
            checkBox.IsChecked = value.GetBoolean();
        }
        else if (editor is TextBox textBox)
        {
            textBox.Text = value.ValueKind == JsonValueKind.String
                ? value.GetString() ?? string.Empty
                : value.GetRawText();
        }
        errorText.Text = string.Empty;
    }

    private void Save_Click(object sender, RoutedEventArgs e)
    {
        try
        {
            var values = new Dictionary<string, JsonElement>(StringComparer.Ordinal);
            foreach (var (key, defaultValue) in defaults)
            {
                values[key] = editors[key] switch
                {
                    CheckBox checkBox => JsonSerializer.SerializeToElement(checkBox.IsChecked == true),
                    TextBox textBox when defaultValue.ValueKind == JsonValueKind.String =>
                        JsonSerializer.SerializeToElement(textBox.Text),
                    TextBox textBox when double.TryParse(
                        textBox.Text, NumberStyles.Float, CultureInfo.InvariantCulture, out var number) &&
                        double.IsFinite(number) => JsonSerializer.SerializeToElement(number),
                    TextBox => throw new FormatException($"{key} must be a finite number."),
                    _ => defaultValue.Clone()
                };
            }
            Configuration = values;
            DialogResult = true;
        }
        catch (FormatException exception)
        {
            errorText.Text = exception.Message;
        }
    }

    private static Button MakeButton(string content) => new()
    {
        Content = content,
        Padding = new Thickness(14, 7, 14, 7),
        Foreground = Brushes.White,
        Background = new SolidColorBrush(Color.FromRgb(42, 53, 67)),
        BorderBrush = new SolidColorBrush(Color.FromRgb(59, 74, 91))
    };
}
