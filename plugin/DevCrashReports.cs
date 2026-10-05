using HarmonyLib;
using Sentry.Unity;

namespace KoH2Upscale
{
    // Test runs crash on purpose; KOH2UPSCALE_NO_CRASH_REPORTS=1 keeps those reports from reaching the game's developers.
    [HarmonyPatch(typeof(SentryOptionsConfiguration), nameof(SentryOptionsConfiguration.Configure))]
    static class DevCrashReports
    {
        internal static bool Wanted => System.Environment.GetEnvironmentVariable("KOH2UPSCALE_NO_CRASH_REPORTS") == "1";

        static void Postfix(SentryUnityOptions options)
        {
            options.Enabled = false;
            options.WindowsNativeSupportEnabled = false;
            Plugin.Log.LogInfo("crash reporting disabled for this run");
        }
    }
}
