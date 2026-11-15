using System.Text;

namespace ProperNotes.WindowsHost;

internal static class Program
{
    [STAThread]
    private static int Main(string[] args)
    {
        ApplicationConfiguration.Initialize();
        HostOptions options;
        try { options = HostOptions.Parse(args); HostLog.Initialize(options.Root); }
        catch (Exception error) { ShowError(error.Message); return 10; }
        if (options.Mode == HostMode.Activate)
        {
            try
            {
                var result = HostSingleInstance.ActivateAsync(options.Root).GetAwaiter().GetResult();
                if (result == 3) ShowError("An existing ProperNotes window is not responding to activation. No second window or service was started.");
                return result;
            }
            catch (Exception error) { HostLog.Write(error.ToString()); ShowError(error.Message); return 3; }
        }
        RuntimePrerequisites runtime;
        try { runtime = RuntimePrerequisites.Check(options); }
        catch (Exception error) { HostLog.Write(error.ToString()); ShowError(RuntimePrerequisites.Describe(error)); return 20; }
        if (options.Mode == HostMode.CheckRuntime) return 0;
        try
        {
            using var singleInstance = HostSingleInstance.Acquire(options.Root);
            if (!singleInstance.OwnsInstance)
            {
                var activated = HostSingleInstance.ActivateAsync(options.Root).GetAwaiter().GetResult();
                if (activated != 0) ShowError("ProperNotes is already opening or running. No second window was created.");
                return activated == 0 ? 0 : 3;
            }
            using var window = new AppWindow(options, runtime, singleInstance);
            Application.Run(window);
            return window.ExitCode;
        }
        catch (Exception error) { HostLog.Write(error.ToString()); ShowError(error.Message); return 21; }
    }

    internal static void ShowError(string message) => MessageBox.Show(message, HostOptions.Product, MessageBoxButtons.OK, MessageBoxIcon.Error);
}

internal static class HostLog
{
    private static readonly object Gate = new();
    private static string? _path;
    internal static void Initialize(string root)
    {
        var directory = Path.Combine(root, "Logs");
        Directory.CreateDirectory(directory);
        _path = Path.Combine(directory, "host.log");
    }
    internal static void Write(string message)
    {
        lock (Gate)
        {
            try
            {
                if (_path is null || File.Exists(_path) && new FileInfo(_path).Length > 4 * 1024 * 1024) return;
                File.AppendAllText(_path, DateTimeOffset.Now.ToString("O") + " " + message + Environment.NewLine, new UTF8Encoding(false));
            }
            catch { /* A diagnostic failure must not confirm a save or close. */ }
        }
    }
}
