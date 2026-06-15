namespace BBCLauncher.Models
{
    public sealed class ResolutionOption
    {
        public string Label { get; set; }
        public int Width { get; set; }
        public int Height { get; set; }

        public static ResolutionOption[] SeriesXDefaults { get; } = new[]
        {
            new ResolutionOption { Label = "720p", Width = 1280, Height = 720 },
            new ResolutionOption { Label = "1080p", Width = 1920, Height = 1080 },
            new ResolutionOption { Label = "1440p", Width = 2560, Height = 1440 },
            new ResolutionOption { Label = "4K", Width = 3840, Height = 2160 },
        };
    }
}
