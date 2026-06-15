using Windows.ApplicationModel;
using Windows.ApplicationModel.Activation;
using Windows.UI.Xaml;
using Windows.UI.Xaml.Controls;
using Windows.UI.Xaml.Navigation;
using System;
using System.Threading.Tasks;
using BBCLauncher.Configuration;
using BBCLauncher.Services;
using Windows.UI.ViewManagement;

namespace BBCLauncher
{
    public sealed partial class App : Application
    {
        public static LauncherConfig Config { get; private set; }

        public App()
        {
            InitializeComponent();
            Suspending += OnSuspending;
            UnhandledException += OnUnhandledException;
            AppDomain.CurrentDomain.UnhandledException += OnDomainUnhandledException;
            TaskScheduler.UnobservedTaskException += OnUnobservedTaskException;
        }

        protected override async void OnLaunched(LaunchActivatedEventArgs e)
        {
            try
            {
                ApplicationViewScaling.TrySetDisableLayoutScaling(true);
            }
            catch
            {
            }

            Config = await LauncherConfig.LoadAsync();
            await PackagedPayloadBootstrapper.CopyToLocalStateIfPresentAsync();
            var root = Window.Current.Content as Frame;
            if (root == null)
            {
                root = new Frame();
                root.NavigationFailed += OnNavigationFailed;
                Window.Current.Content = root;
            }

            if (root.Content == null)
            {
                if (Config.GraphicsProbeMode)
                {
                    root.Navigate(typeof(GraphicsProbePage), e.Arguments);
                }
                else if (Config.EnableResolutionPicker)
                {
                    root.Navigate(typeof(ResolutionPickerPage), e.Arguments);
                }
                else
                {
                    root.Navigate(typeof(MainPage), e.Arguments);
                }
            }

            Window.Current.Activate();
        }

        private void OnNavigationFailed(object sender, NavigationFailedEventArgs e)
        {
            LaunchLog.AppendSharedLine(
                LaunchLog.CurrentSharedLogPath,
                "[App][NavigationFailed] " + e.SourcePageType.FullName);
            throw new System.Exception("Failed to load Page " + e.SourcePageType.FullName);
        }

        private void OnSuspending(object sender, SuspendingEventArgs e)
        {
            LaunchLog.AppendSharedLine(LaunchLog.CurrentSharedLogPath, "[App] Suspending");
            var deferral = e.SuspendingOperation.GetDeferral();
            deferral.Complete();
        }

        private void OnUnhandledException(object sender, Windows.UI.Xaml.UnhandledExceptionEventArgs e)
        {
            LaunchLog.AppendSharedLine(
                LaunchLog.CurrentSharedLogPath,
                "[App][UnhandledException] " + e.Message + " " + e.Exception);
        }

        private void OnDomainUnhandledException(object sender, System.UnhandledExceptionEventArgs e)
        {
            LaunchLog.AppendSharedLine(
                LaunchLog.CurrentSharedLogPath,
                "[AppDomain][UnhandledException] terminating=" + e.IsTerminating + " " + e.ExceptionObject);
        }

        private void OnUnobservedTaskException(object sender, UnobservedTaskExceptionEventArgs e)
        {
            LaunchLog.AppendSharedLine(
                LaunchLog.CurrentSharedLogPath,
                "[Task][UnobservedException] " + e.Exception);
            e.SetObserved();
        }
    }
}
