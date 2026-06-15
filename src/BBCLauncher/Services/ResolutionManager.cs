using System;
using System.IO;
using System.Threading.Tasks;
using BBCLauncher.Models;
using Newtonsoft.Json;
using Windows.Storage;

namespace BBCLauncher.Services
{
    public sealed class LaunchResolutionSettings
    {
        [JsonProperty("label")]
        public string Label { get; set; }

        [JsonProperty("width")]
        public int Width { get; set; }

        [JsonProperty("height")]
        public int Height { get; set; }
    }

    public static class ResolutionManager
    {
        public const string SettingsFileName = "launch-resolution.json";

        public static async Task SaveAsync(StorageFolder localFolder, ResolutionOption resolution)
        {
            if (localFolder == null || resolution == null)
            {
                return;
            }

            var payload = new LaunchResolutionSettings
            {
                Label = resolution.Label,
                Width = resolution.Width,
                Height = resolution.Height,
            };

            var file = await localFolder.CreateFileAsync(
                SettingsFileName,
                CreationCollisionOption.ReplaceExisting);
            await FileIO.WriteTextAsync(file, JsonConvert.SerializeObject(payload));
        }

        public static async Task<ResolutionOption> LoadAsync(StorageFolder localFolder)
        {
            if (localFolder == null)
            {
                return null;
            }

            try
            {
                var file = await localFolder.GetFileAsync(SettingsFileName);
                var json = await FileIO.ReadTextAsync(file);
                var payload = JsonConvert.DeserializeObject<LaunchResolutionSettings>(json);
                if (payload == null || payload.Width <= 0 || payload.Height <= 0)
                {
                    return null;
                }

                return new ResolutionOption
                {
                    Label = string.IsNullOrWhiteSpace(payload.Label)
                        ? payload.Width + "x" + payload.Height
                        : payload.Label,
                    Width = payload.Width,
                    Height = payload.Height,
                };
            }
            catch (FileNotFoundException)
            {
                return null;
            }
            catch (UnauthorizedAccessException)
            {
                return null;
            }
        }
    }
}
