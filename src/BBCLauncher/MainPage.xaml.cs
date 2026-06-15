using System;
using System.Threading;
using Windows.UI.Xaml.Controls;
using Windows.UI.Xaml.Navigation;
using BBCLauncher.Models;
using BBCLauncher.Services;
using Windows.Storage;

namespace BBCLauncher
{
    public sealed partial class MainPage : Page
    {
        private readonly LaunchOrchestrator _orchestrator;
        private readonly CancellationTokenSource _cts = new CancellationTokenSource();
        private ResolutionOption _resolution;

        public MainPage()
        {
            InitializeComponent();
            _orchestrator = new LaunchOrchestrator(App.Config);
            _orchestrator.StageChanged += OnStageChanged;
        }

        protected override void OnNavigatedTo(NavigationEventArgs e)
        {
            base.OnNavigatedTo(e);
            _resolution = e.Parameter as ResolutionOption ?? ResolutionOption.SeriesXDefaults[2];
            _ = StartLaunchAsync();
        }

        private async System.Threading.Tasks.Task StartLaunchAsync()
        {
            StatusText.Text = string.Empty;
            DiagnosticsBox.Text = "Running preflight and launch diagnostics...";

            var local = ApplicationData.Current.LocalFolder;
            var result = await _orchestrator.RunAsync(local, _resolution, _cts.Token);

            if (result.IsRunning)
            {
                if (App.Config.EnableGameHostPresentation)
                {
                    Frame.Navigate(typeof(GameHostPage), new GameHostLaunchContext
                    {
                        LaunchResult = result,
                        Resolution = _resolution,
                    });
                    return;
                }

                StatusText.Foreground = new Windows.UI.Xaml.Media.SolidColorBrush(Windows.UI.Colors.LightGreen);
                StatusText.Text = "Minecraft started. Log: " + result.LogPath;
            }
            else if (result.IsProbe)
            {
                StatusText.Foreground = new Windows.UI.Xaml.Media.SolidColorBrush(Windows.UI.Colors.LightGreen);
                StatusText.Text = result.Message;
            }
            else if (result.IsCancelled)
            {
                StatusText.Text = result.Message;
            }
            else
            {
                StatusText.Text = result.Message ?? "Launch failed.";
            }

            ProgressBar.IsIndeterminate = false;
            ProgressBar.Value = result.IsSuccess ? 100 : 0;
            DiagnosticsBox.Text = string.IsNullOrWhiteSpace(result.Diagnostics)
                ? "No diagnostics captured."
                : result.Diagnostics;
        }

        private void OnStageChanged(LaunchStage stage, string detail)
        {
            _ = Dispatcher.RunAsync(Windows.UI.Core.CoreDispatcherPriority.Normal, () =>
            {
                StageText.Text = stage.ToString();
                DetailText.Text = detail ?? string.Empty;
            });
        }
    }
}
