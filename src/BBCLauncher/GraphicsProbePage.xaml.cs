using Windows.UI;
using Windows.UI.Xaml;
using Windows.UI.Xaml.Controls;
using Windows.UI.Xaml.Media;
using Windows.UI.Xaml.Shapes;
using BBCLauncher.Services;
using Windows.Storage;

namespace BBCLauncher
{
    public sealed partial class GraphicsProbePage : Page
    {
        public GraphicsProbePage()
        {
            InitializeComponent();
            Loaded += async (_, __) => await RunProbeAsync();
        }

        private async System.Threading.Tasks.Task RunProbeAsync()
        {
            ProbeCanvas.Children.Clear();
            var colors = new[] { Colors.Coral, Colors.MediumSeaGreen, Colors.SteelBlue, Colors.Goldenrod };
            for (var i = 0; i < colors.Length; i++)
            {
                ProbeCanvas.Children.Add(new Rectangle
                {
                    Width = 200,
                    Height = 200,
                    Fill = new SolidColorBrush(colors[i]),
                    Margin = new Thickness(40 + i * 220, 120, 0, 0),
                });
            }

            var logsDir = App.Config.GetLogsRoot(ApplicationData.Current.LocalFolder);
            using (var log = await LaunchLog.CreateAsync(logsDir))
            {
                var staged = await new RuntimeAssetStager(App.Config).StageAsync(ApplicationData.Current.LocalFolder);
                foreach (var line in staged.Lines)
                {
                    await log.WriteLineAsync(line);
                }

                var probe = new GraphicsProbeService(App.Config);
                var result = await probe.RunProbeAsync(log);
                ProbeStatus.Text = result.Message + " | " + result.Renderer + " " + result.Version;
            }
        }

        private async void OnRetryClicked(object sender, RoutedEventArgs e) => await RunProbeAsync();
    }
}
