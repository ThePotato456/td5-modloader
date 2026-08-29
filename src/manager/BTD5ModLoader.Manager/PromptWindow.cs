using System.Windows;
using System.Windows.Controls;

namespace BTD5ModLoader.Manager;

internal sealed class PromptWindow : Window
{
    private readonly TextBox input = new();

    private PromptWindow(string title, string prompt, string initialValue)
    {
        Title = title;
        Width = 410;
        Height = 175;
        ResizeMode = ResizeMode.NoResize;
        WindowStartupLocation = WindowStartupLocation.CenterOwner;
        Background = new System.Windows.Media.SolidColorBrush(
            System.Windows.Media.Color.FromRgb(21, 29, 39));
        Foreground = System.Windows.Media.Brushes.White;
        var panel = new StackPanel { Margin = new Thickness(18) };
        panel.Children.Add(new TextBlock
        {
            Text = prompt,
            Foreground = System.Windows.Media.Brushes.White,
            Margin = new Thickness(0, 0, 0, 8)
        });
        input.Text = initialValue;
        input.Foreground = System.Windows.Media.Brushes.White;
        input.Background = new System.Windows.Media.SolidColorBrush(
            System.Windows.Media.Color.FromRgb(16, 23, 32));
        input.BorderBrush = new System.Windows.Media.SolidColorBrush(
            System.Windows.Media.Color.FromRgb(52, 67, 84));
        input.Padding = new Thickness(8, 6, 8, 6);
        input.SelectAll();
        panel.Children.Add(input);
        var buttons = new StackPanel
        {
            Orientation = Orientation.Horizontal,
            HorizontalAlignment = HorizontalAlignment.Right,
            Margin = new Thickness(0, 14, 0, 0)
        };
        var cancel = MakeButton("Cancel");
        cancel.Click += (_, _) => DialogResult = false;
        var accept = MakeButton("Save");
        accept.IsDefault = true;
        accept.Margin = new Thickness(8, 0, 0, 0);
        accept.Background = new System.Windows.Media.SolidColorBrush(
            System.Windows.Media.Color.FromRgb(35, 135, 227));
        accept.Click += (_, _) => DialogResult = true;
        buttons.Children.Add(cancel);
        buttons.Children.Add(accept);
        panel.Children.Add(buttons);
        Content = panel;
        Loaded += (_, _) => input.Focus();
    }

    public static string? Ask(Window owner, string title, string prompt, string initialValue)
    {
        var window = new PromptWindow(title, prompt, initialValue) { Owner = owner };
        return window.ShowDialog() == true ? window.input.Text.Trim() : null;
    }

    private static Button MakeButton(string content) => new()
    {
        Content = content,
        Padding = new Thickness(14, 6, 14, 6),
        Foreground = System.Windows.Media.Brushes.White,
        Background = new System.Windows.Media.SolidColorBrush(
            System.Windows.Media.Color.FromRgb(42, 53, 67)),
        BorderBrush = new System.Windows.Media.SolidColorBrush(
            System.Windows.Media.Color.FromRgb(59, 74, 91))
    };
}
