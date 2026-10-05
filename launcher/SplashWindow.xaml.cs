using System.IO;
using System.Windows;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Animation;
using System.Windows.Media.Imaging;

namespace Vendetta;

public partial class SplashWindow : Window
{
    readonly bool _reduce = !SystemParameters.ClientAreaAnimation;
    bool _failed;

    public bool Finished { get; private set; }

    public SplashWindow()
    {
        InitializeComponent();
        var font = Pick("Manrope", "Segoe UI");
        Word.FontFamily = font;
        StatusText.FontFamily = Pick("Outfit", "Segoe UI");
        CloseButton.FontFamily = StatusText.FontFamily;
        TryIcon();
        if (_reduce)
            EmberShift.X = 58;
    }

    public void MarkFinished() => Finished = true;

    public void SetStatus(string text)
    {
        if (!Dispatcher.CheckAccess())
        {
            Dispatcher.Invoke(() => SetStatus(text));
            return;
        }
        if (_failed || text.Length == 0)
            return;
        StatusText.Text = text;
    }

    public void Fail(string text)
    {
        if (!Dispatcher.CheckAccess())
        {
            Dispatcher.Invoke(() => Fail(text));
            return;
        }
        _failed = true;
        EmberShift.BeginAnimation(TranslateTransform.XProperty, null);
        StatusText.Text = text;
        StatusText.Foreground = new SolidColorBrush(Color.FromRgb(0xE0, 0x54, 0x54));
        CloseButton.Visibility = Visibility.Visible;
        Track.Visibility = Visibility.Collapsed;
    }

    public void CaptureAfter(TimeSpan delay, string path)
    {
        var timer = new System.Windows.Threading.DispatcherTimer { Interval = delay };
        timer.Tick += (_, _) =>
        {
            timer.Stop();
            var dpi = VisualTreeHelper.GetDpi(this);
            var width = Math.Max(1, (int)(ActualWidth * dpi.DpiScaleX));
            var height = Math.Max(1, (int)(ActualHeight * dpi.DpiScaleY));
            var bitmap = new RenderTargetBitmap(width, height, dpi.PixelsPerInchX, dpi.PixelsPerInchY, PixelFormats.Pbgra32);
            bitmap.Render(this);
            var encoder = new PngBitmapEncoder();
            encoder.Frames.Add(BitmapFrame.Create(bitmap));
            Directory.CreateDirectory(Path.GetDirectoryName(path)!);
            using var file = File.Create(path);
            encoder.Save(file);
        };
        timer.Start();
    }

    void OnLoaded(object sender, RoutedEventArgs e)
    {
        if (_reduce)
            return;
        EmberShift.BeginAnimation(TranslateTransform.XProperty, new DoubleAnimation(-64, 180, TimeSpan.FromSeconds(0.9))
        {
            RepeatBehavior = RepeatBehavior.Forever,
        });
    }

    void OnDrag(object sender, MouseButtonEventArgs e)
    {
        if (e.OriginalSource is System.Windows.Controls.Button)
            return;
        try { DragMove(); } catch { /* the button owns this press */ }
    }

    void OnClose(object sender, RoutedEventArgs e) => Close();

    void TryIcon()
    {
        var dir = new DirectoryInfo(AppContext.BaseDirectory);
        while (dir != null)
        {
            var icon = Path.Combine(dir.FullName, "shell", "vendetta.png");
            if (File.Exists(icon))
            {
                Icon = BitmapFrame.Create(new Uri(icon));
                return;
            }
            dir = dir.Parent;
        }
    }

    static FontFamily Pick(params string[] names)
    {
        foreach (var name in names)
        {
            foreach (var family in Fonts.SystemFontFamilies)
            {
                foreach (var known in family.FamilyNames.Values)
                {
                    if (known.Equals(name, StringComparison.OrdinalIgnoreCase))
                        return family;
                }
            }
        }
        return new FontFamily("Segoe UI");
    }
}
