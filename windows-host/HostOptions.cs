using System.Net.Http;
using System.Text.Json;
using System.Text.RegularExpressions;

namespace ProperNotes.WindowsHost;

internal enum HostMode { Window, Activate, CheckRuntime }

internal sealed record HostOptions(HostMode Mode, string Root, Uri? Url, string? Instance, string? Revision, string UserData)
{
    internal const string Product = "ProperNotes for Windows";
    internal string Origin => Url!.GetLeftPart(UriPartial.Authority);

    internal static HostOptions Parse(string[] arguments)
    {
        var values = new Dictionary<string, string>(StringComparer.Ordinal);
        var mode = HostMode.Window;
        for (var index = 0; index < arguments.Length; index++)
        {
            var key = arguments[index];
            if (key is "--activate" or "--check-runtime")
            {
                if (mode != HostMode.Window) throw new ArgumentException("Only one host mode may be specified.");
                mode = key == "--activate" ? HostMode.Activate : HostMode.CheckRuntime;
                continue;
            }
            if (key is not ("--url" or "--instance" or "--revision" or "--root" or "--user-data") || ++index >= arguments.Length || !values.TryAdd(key, arguments[index]))
                throw new ArgumentException("Invalid host arguments. Start ProperNotes with Start-ProperNotes.cmd.");
        }
        if (!values.TryGetValue("--root", out var rawRoot) || !Path.IsPathFullyQualified(rawRoot))
            throw new ArgumentException("An absolute portable package root is required.");
        var root = Path.TrimEndingDirectorySeparator(Path.GetFullPath(rawRoot));
        if (!Directory.Exists(root) || root.StartsWith(@"\\", StringComparison.Ordinal) || !File.Exists(Path.Combine(root, "server.cjs")) || !File.Exists(Path.Combine(root, "Start-ProperNotes.cmd")))
            throw new ArgumentException("The portable package root is missing, incomplete, or on an unsupported network share.");
        var expectedHost = Path.Combine(root, "host");
        if (!string.Equals(Path.TrimEndingDirectorySeparator(Path.GetFullPath(AppContext.BaseDirectory)), expectedHost, StringComparison.OrdinalIgnoreCase))
            throw new ArgumentException("Keep the published host folder inside the complete portable package.");
        var userData = Path.Combine(root, "UserData", "WebView2");
        if (mode != HostMode.Window)
        {
            if (values.Count != 1) throw new ArgumentException("This host mode accepts only --root.");
            return new(mode, root, null, null, null, userData);
        }
        if (values.Count != 5 || !values.TryGetValue("--url", out var rawUrl) || !Uri.TryCreate(rawUrl, UriKind.Absolute, out var url)
            || url.Scheme != Uri.UriSchemeHttp || url.Host != "127.0.0.1" || !url.IsLoopback || url.Port < 1024 || url.Port > 65535
            || !string.IsNullOrEmpty(url.UserInfo) || !string.IsNullOrEmpty(url.Query) || !string.IsNullOrEmpty(url.Fragment) || url.AbsolutePath != "/")
            throw new ArgumentException("The host only accepts the selected loopback service root URL.");
        if (!values.TryGetValue("--instance", out var instance) || !Hash(instance) || !values.TryGetValue("--revision", out var revision) || !Hash(revision))
            throw new ArgumentException("The prepared package identity and revision are required.");
        if (!values.TryGetValue("--user-data", out var rawUserData) || !Path.IsPathFullyQualified(rawUserData)
            || !string.Equals(Path.TrimEndingDirectorySeparator(Path.GetFullPath(rawUserData)), userData, StringComparison.OrdinalIgnoreCase))
            throw new ArgumentException("The portable WebView2 profile must be UserData\\WebView2 inside this package.");
        return new(mode, root, url, instance, revision, userData);
    }

    internal static bool Hash(string? value) => value is not null && Regex.IsMatch(value, "^[a-f0-9]{64}$", RegexOptions.CultureInvariant);

    internal bool IsAppAddress(string address)
    {
        return Uri.TryCreate(address, UriKind.Absolute, out var uri) && uri.Scheme == Uri.UriSchemeHttp && uri.Host == "127.0.0.1"
            && uri.Port == Url!.Port && string.IsNullOrEmpty(uri.UserInfo) && string.IsNullOrEmpty(uri.Query)
            && (uri.AbsolutePath == "/" || uri.AbsolutePath == "/index.html");
    }

    internal async Task VerifyServiceAsync(CancellationToken cancellationToken)
    {
        using var handler = new HttpClientHandler { AllowAutoRedirect = false, UseProxy = false };
        using var client = new HttpClient(handler) { BaseAddress = Url, Timeout = TimeSpan.FromSeconds(8) };
        foreach (var resource in new[] { "__inkpdf_health", "build-info.json" })
        {
            using var response = await client.GetAsync(resource, HttpCompletionOption.ResponseHeadersRead, cancellationToken);
            if (!response.IsSuccessStatusCode || response.Content.Headers.ContentLength is > 65536)
                throw new InvalidOperationException("The local service did not return the expected prepared package identity.");
            await using var stream = await response.Content.ReadAsStreamAsync(cancellationToken);
            using var bounded = new MemoryStream();
            var buffer = new byte[4096];
            int count;
            while ((count = await stream.ReadAsync(buffer, cancellationToken)) != 0)
            {
                if (bounded.Length + count > 65536) throw new InvalidOperationException("The local service identity response is too large.");
                await bounded.WriteAsync(buffer.AsMemory(0, count), cancellationToken);
            }
            using var document = JsonDocument.Parse(bounded.ToArray());
            var value = document.RootElement;
            if (!Property(value, "app", "inkpdf-local") || !Property(value, "instance", Instance!) || !Property(value, "revision", Revision!)
                || resource == "__inkpdf_health" && (!value.TryGetProperty("apiVersion", out var api) || !api.TryGetInt32(out var version) || version < 2))
                throw new InvalidOperationException("The local service belongs to another folder or source revision. No document was opened.");
        }
    }

    private static bool Property(JsonElement value, string name, string expected) => value.ValueKind == JsonValueKind.Object
        && value.TryGetProperty(name, out var item) && item.ValueKind == JsonValueKind.String && item.GetString() == expected;
}
