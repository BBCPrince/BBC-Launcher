using BBCLauncher.Models;

namespace BBCLauncher.Services
{
    public sealed class GameHostLaunchContext
    {
        public LaunchResult LaunchResult { get; set; }
        public ResolutionOption Resolution { get; set; }
    }
}
