using System.IO.Pipes;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Security.Principal;
using System.Text;
using Microsoft.Win32.SafeHandles;

namespace ProperNotes.WindowsHost;

internal sealed class HostSingleInstance : IDisposable
{
    private readonly Mutex _mutex;
    private readonly CancellationTokenSource _stop = new();
    private readonly string _pipeName;
    private readonly bool _owns;
    private Task? _listener;
    private HostSingleInstance(Mutex mutex, string pipeName, bool owns) { _mutex = mutex; _pipeName = pipeName; _owns = owns; }

    private static string Identity(string root)
    {
        using var identity = WindowsIdentity.GetCurrent();
        var user = identity.User?.Value ?? Environment.UserName;
        return Convert.ToHexStringLower(SHA256.HashData(Encoding.UTF8.GetBytes(user + "\0" + root.ToUpperInvariant())));
    }
    internal static HostSingleInstance Acquire(string root)
    {
        var identity = Identity(root);
        var mutex = new Mutex(false, @"Local\ProperNotes.Host." + identity);
        var owns = false;
        try { owns = mutex.WaitOne(0); }
        catch (AbandonedMutexException) { owns = true; }
        return new(mutex, "ProperNotes.Host." + identity, owns);
    }
    internal bool OwnsInstance => _owns;

    internal static async Task<int> ActivateAsync(string root)
    {
        var identity = Identity(root);
        try
        {
            using var mutex = Mutex.OpenExisting(@"Local\ProperNotes.Host." + identity);
            var acquired = false;
            try { acquired = mutex.WaitOne(0); }
            catch (AbandonedMutexException) { acquired = true; }
            if (acquired) { mutex.ReleaseMutex(); return 2; }
        }
        catch (WaitHandleCannotBeOpenedException) { return 2; }
        try
        {
            using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(3));
            await using var pipe = new NamedPipeClientStream(".", "ProperNotes.Host." + identity, PipeDirection.InOut, PipeOptions.Asynchronous);
            await pipe.ConnectAsync(timeout.Token);
            // Explicit second launch may grant the existing window foreground
            // permission; no process handle or termination capability is shared.
            if (GetNamedPipeServerProcessId(pipe.SafePipeHandle, out var processId)) AllowSetForegroundWindow(processId);
            await pipe.WriteAsync("focus"u8.ToArray(), timeout.Token);
            await pipe.FlushAsync(timeout.Token);
            var reply = new byte[2];
            await pipe.ReadExactlyAsync(reply, timeout.Token);
            return reply.AsSpan().SequenceEqual("ok"u8) ? 0 : 3;
        }
        catch (Exception error) when (error is IOException or OperationCanceledException or UnauthorizedAccessException)
        {
            HostLog.Write("Existing window activation failed: " + error.Message);
            return 3;
        }
    }

    internal void Listen(Action focus)
    {
        if (!_owns || _listener is not null) return;
        _listener = ListenAsync(focus);
    }
    private async Task ListenAsync(Action focus)
    {
        while (!_stop.IsCancellationRequested)
        {
            try
            {
                await using var pipe = new NamedPipeServerStream(_pipeName, PipeDirection.InOut, 1, PipeTransmissionMode.Byte,
                    PipeOptions.Asynchronous | PipeOptions.CurrentUserOnly);
                await pipe.WaitForConnectionAsync(_stop.Token);
                using var timeout = CancellationTokenSource.CreateLinkedTokenSource(_stop.Token);
                timeout.CancelAfter(TimeSpan.FromSeconds(2));
                var request = new byte[5];
                await pipe.ReadExactlyAsync(request, timeout.Token);
                if (!request.AsSpan().SequenceEqual("focus"u8)) continue;
                focus();
                await pipe.WriteAsync("ok"u8.ToArray(), timeout.Token);
                await pipe.FlushAsync(timeout.Token);
            }
            catch (Exception error) when (error is IOException or OperationCanceledException or UnauthorizedAccessException or ObjectDisposedException)
            {
                if (!_stop.IsCancellationRequested)
                {
                    HostLog.Write("Window activation pipe: " + error.Message);
                    // Pipe construction can fail synchronously (for example an
                    // ACL or a colliding pipe). Yield instead of spinning the UI.
                    try { await Task.Delay(200, _stop.Token); }
                    catch (OperationCanceledException) { }
                }
            }
        }
    }
    public void Dispose()
    {
        _stop.Cancel();
        if (_owns) _mutex.ReleaseMutex();
        _mutex.Dispose();
        // The canceled asynchronous pipe owns its own disposal path.
    }

    [DllImport("kernel32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool GetNamedPipeServerProcessId(SafePipeHandle pipe, out uint serverProcessId);
    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool AllowSetForegroundWindow(uint processId);
}
