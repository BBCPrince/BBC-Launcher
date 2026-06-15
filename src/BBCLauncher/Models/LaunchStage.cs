namespace BBCLauncher.Models
{
    public enum LaunchStage
    {
        Idle,
        PreparingStorage,
        VerifyingCache,
        Downloading,
        Authenticating,
        CheckingEntitlement,
        StartingGraphics,
        LaunchingJava,
        Running,
        Failed,
        Cancelled,
    }
}
