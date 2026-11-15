using Microsoft.Web.WebView2.Core;

namespace ProperNotes.WindowsHost;

internal sealed record RuntimePrerequisites(string? BrowserFolder, string Version)
{
    private static void ConfigureProcess(HostOptions options, string? browserFolder)
    {
        // WebView2 also reads inherited environment variables and registry
        // overrides. These values affect this host process only; never persist
        // them to the user's or machine's environment or change policy keys.
        Environment.SetEnvironmentVariable("WEBVIEW2_USER_DATA_FOLDER", options.UserData, EnvironmentVariableTarget.Process);
        Environment.SetEnvironmentVariable("WEBVIEW2_BROWSER_EXECUTABLE_FOLDER", browserFolder, EnvironmentVariableTarget.Process);
        Environment.SetEnvironmentVariable("WEBVIEW2_RELEASE_CHANNELS", "0", EnvironmentVariableTarget.Process);
        Environment.SetEnvironmentVariable("WEBVIEW2_CHANNEL_SEARCH_KIND", null, EnvironmentVariableTarget.Process);
        Environment.SetEnvironmentVariable("WEBVIEW2_RELEASE_CHANNEL_PREFERENCE", null, EnvironmentVariableTarget.Process);
        Environment.SetEnvironmentVariable("WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS", null, EnvironmentVariableTarget.Process);
        Environment.SetEnvironmentVariable("WEBVIEW2_WAIT_FOR_SCRIPT_DEBUGGER", null, EnvironmentVariableTarget.Process);
        Environment.SetEnvironmentVariable("WEBVIEW2_PIPE_FOR_SCRIPT_DEBUGGER", null, EnvironmentVariableTarget.Process);
    }

    internal static CoreWebView2EnvironmentOptions EnvironmentOptions() => new()
    {
        // Stable here means the production WebView2 Runtime, not Edge Stable.
        ReleaseChannels = CoreWebView2ReleaseChannels.Stable,
        AllowSingleSignOnUsingOSPrimaryAccount = false
    };

    internal static RuntimePrerequisites Check(HostOptions options)
    {
        var folder = Path.Combine(options.Root, "runtime", "WebView2");
        if (File.Exists(folder)) throw new InvalidOperationException("runtime\\WebView2 must be a complete runtime directory, not a file.");
        string? browserFolder = Directory.Exists(folder) ? folder : null;
        if (browserFolder is not null && !File.Exists(Path.Combine(browserFolder, "msedgewebview2.exe")))
            throw new InvalidOperationException("The bundled Fixed Version WebView2 runtime is incomplete. Keep its entire folder; no runtime was installed.");
        ConfigureProcess(options, browserFolder);
        // Check both startup modes without creating a browser environment,
        // window or service. Missing native loader/runtime is a packaging error.
        var version = CoreWebView2Environment.GetAvailableBrowserVersionString(browserFolder, EnvironmentOptions());
        if (string.IsNullOrWhiteSpace(version) || version.Any(char.IsLetter))
            throw new InvalidOperationException("A production WebView2 Runtime is required. Preview Edge channels are not used.");
        return new(browserFolder, version);
    }

    internal static string Describe(Exception error) =>
        "The Windows window host could not use WebView2. Keep all files in the published host folder and use an existing Evergreen Runtime or a complete matching Fixed Version runtime in runtime\\WebView2. "
        + "Nothing was downloaded or installed, and no browser fallback was opened.\n\n" + error.Message;
}
