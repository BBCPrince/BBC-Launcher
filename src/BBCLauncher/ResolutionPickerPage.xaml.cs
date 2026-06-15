using System.Collections.Generic;
using System.Linq;
using BBCLauncher.Models;
using BBCLauncher.Services;
using Windows.Storage;
using Windows.UI.Xaml;
using Windows.UI.Xaml.Controls;
using Windows.UI.Xaml.Navigation;

namespace BBCLauncher
{
    public sealed partial class ResolutionPickerPage : Page
    {
        private sealed class ResolutionListItem
        {
            public ResolutionOption Option { get; set; }
            public string Label { get; set; }
            public string Detail { get; set; }
        }

        private List<ResolutionListItem> _items;

        public ResolutionPickerPage()
        {
            InitializeComponent();
        }

        protected override async void OnNavigatedTo(NavigationEventArgs e)
        {
            base.OnNavigatedTo(e);

            _items = ResolutionOption.SeriesXDefaults
                .Select(option => new ResolutionListItem
                {
                    Option = option,
                    Label = option.Label,
                    Detail = option.Width + " x " + option.Height,
                })
                .ToList();

            ResolutionList.ItemsSource = _items;

            var saved = await ResolutionManager.LoadAsync(ApplicationData.Current.LocalFolder);
            var selectedIndex = 2;
            if (saved != null)
            {
                for (var i = 0; i < _items.Count; i++)
                {
                    if (_items[i].Option.Width == saved.Width && _items[i].Option.Height == saved.Height)
                    {
                        selectedIndex = i;
                        break;
                    }
                }
            }

            ResolutionList.SelectedIndex = selectedIndex;
        }

        private void OnResolutionItemClick(object sender, ItemClickEventArgs e)
        {
            ResolutionList.SelectedItem = e.ClickedItem;
        }

        private async void OnContinueClick(object sender, RoutedEventArgs e)
        {
            var selected = ResolutionList.SelectedItem as ResolutionListItem;
            if (selected?.Option == null)
            {
                return;
            }

            ContinueButton.IsEnabled = false;
            ResolutionList.IsEnabled = false;

            var local = ApplicationData.Current.LocalFolder;
            await ResolutionManager.SaveAsync(local, selected.Option);
            Frame.Navigate(typeof(MainPage), selected.Option);
        }
    }
}
