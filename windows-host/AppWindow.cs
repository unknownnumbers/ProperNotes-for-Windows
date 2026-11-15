using Microsoft.Web.WebView2.Core;
using Microsoft.Web.WebView2.WinForms;
using System.Security.Cryptography;
using System.Text.Json;

namespace ProperNotes.WindowsHost;

internal sealed class AppWindow : Form
{
    private readonly HostOptions _options;
    private readonly RuntimePrerequisites _runtime;
    private readonly HostSingleInstance _singleInstance;
    private readonly WebView2 _view = new() { Dock = DockStyle.Fill, Enabled = false, DefaultBackgroundColor = Color.White };
    private readonly StatusStrip _status = new() { Visible = true, SizingGrip = false };
    private readonly ToolStripStatusLabel _statusText = new() { Spring = true, TextAlign = ContentAlignment.MiddleLeft, Text = "正在打开 ProperNotes…" };
    private readonly CancellationTokenSource _lifetime = new();
    private readonly System.Windows.Forms.Timer _startupTimer = new() { Interval = 30000 };
    private readonly System.Windows.Forms.Timer _closeTimer = new() { Interval = 15000 };
    private readonly string _nonce = Convert.ToHexStringLower(RandomNumberGenerator.GetBytes(32));
    private string? _closeRequest;
    private bool _appReady, _appEverReady, _allowClose, _fullscreen, _started, _resourcesDisposed, _closeConfirmationUnavailable;
    private Rectangle _beforeFullscreen;
    private FormWindowState _beforeFullscreenState;
    internal int ExitCode { get; private set; }

    internal AppWindow(HostOptions options, RuntimePrerequisites runtime, HostSingleInstance singleInstance)
    {
        _options = options; _runtime = runtime; _singleInstance = singleInstance;
        Text = HostOptions.Product;
        StartPosition = FormStartPosition.CenterScreen;
        ClientSize = new Size(1280, 860);
        MinimumSize = new Size(420, 420);
        AutoScaleMode = AutoScaleMode.Dpi;
        _status.Items.Add(_statusText);
        Controls.Add(_view); Controls.Add(_status);
        Shown += async (_, _) => await InitializeAsync();
        FormClosing += ClosingWindow;
        _startupTimer.Tick += (_, _) =>
        {
            _startupTimer.Stop();
            if (!_appReady)
            {
                ExitCode = 21; HostLog.Write("The application bridge did not become ready within 30 seconds.");
                ShowStatus("界面尚未完成初始化。请查看 Logs\\host.log；未收到编辑就绪确认。", true);
            }
        };
        _closeTimer.Tick += (_, _) =>
        {
            _closeTimer.Stop(); _closeRequest = null; _closeConfirmationUnavailable = true;
            ShowStatus("界面尚未确认可安全关闭，窗口已保留。请结束当前操作后重试关闭。", true);
        };
    }

    private async Task InitializeAsync()
    {
        if (_started) return;
        _started = true;
        var working = Screen.FromControl(this).WorkingArea;
        Size = new Size(Math.Min(Width, working.Width), Math.Min(Height, working.Height));
        _singleInstance.Listen(() =>
        {
            if (!IsDisposed && IsHandleCreated) BeginInvoke((Action)(() =>
            {
                if (WindowState == FormWindowState.Minimized) WindowState = FormWindowState.Normal;
                Show(); BringToFront(); Activate(); _view.Focus();
            }));
        });
        try
        {
            await _options.VerifyServiceAsync(_lifetime.Token);
            Directory.CreateDirectory(_options.UserData);
            // A portable profile is deliberate; no registry, user installation,
            // browser profile, or machine permissions are changed by this host.
            var environment = await CoreWebView2Environment.CreateAsync(_runtime.BrowserFolder, _options.UserData, RuntimePrerequisites.EnvironmentOptions());
            if (IsDisposed || _lifetime.IsCancellationRequested) return;
            var actualProfile = Path.TrimEndingDirectorySeparator(Path.GetFullPath(environment.UserDataFolder));
            if (!string.Equals(actualProfile, _options.UserData, StringComparison.OrdinalIgnoreCase))
                throw new InvalidOperationException("WebView2 selected a different user data folder. The local interface was not navigated; the portable profile must remain UserData\\WebView2.");
            if (environment.BrowserVersionString.Any(char.IsLetter)) throw new InvalidOperationException("A preview browser channel was selected instead of the production WebView2 Runtime.");
            await _view.EnsureCoreWebView2Async(environment);
            if (IsDisposed || _lifetime.IsCancellationRequested) return;
            Configure(_view.CoreWebView2, environment);
            _startupTimer.Start();
            _view.CoreWebView2.Navigate(_options.Url!.AbsoluteUri);
        }
        catch (OperationCanceledException) when (_lifetime.IsCancellationRequested) { }
        catch (Exception error)
        {
            if (IsDisposed) return;
            ExitCode = 21; HostLog.Write(error.ToString());
            ShowStatus("窗口初始化失败；没有收到可编辑界面。可关闭窗口，详情见 Logs\\host.log。", true);
            MessageBox.Show(this, error.Message, HostOptions.Product, MessageBoxButtons.OK, MessageBoxIcon.Error);
        }
    }

    private void Configure(CoreWebView2 web, CoreWebView2Environment environment)
    {
        var settings = web.Settings;
        settings.IsWebMessageEnabled = true;
        settings.AreDefaultScriptDialogsEnabled = true;
        // Preserve the existing PDF text selection's native Copy command.
        // Navigation, reload, new windows and downloads are guarded separately.
        settings.AreDefaultContextMenusEnabled = true;
        settings.AreDevToolsEnabled = false;
        settings.IsStatusBarEnabled = false;
        settings.AreBrowserAcceleratorKeysEnabled = false;
        settings.IsZoomControlEnabled = false;
        settings.IsPinchZoomEnabled = false;
        settings.IsSwipeNavigationEnabled = false;
        web.NavigationStarting += (_, args) =>
        {
            if (!_options.IsAppAddress(args.Uri) || _appEverReady)
            {
                args.Cancel = true;
                ShowStatus("已保留当前文档；窗口不离开本地批注界面。", true);
            }
        };
        web.FrameNavigationStarting += (_, args) => { if (!_options.IsAppAddress(args.Uri)) args.Cancel = true; };
        web.NewWindowRequested += (_, args) => { args.Handled = true; ShowStatus("此窗口仅用于本地 PDF；未打开额外网页窗口。", true); };
        web.DownloadStarting += (_, args) => { args.Cancel = true; ShowStatus("未开始浏览器下载。请使用应用内的保存入口。", true); };
        web.PermissionRequested += (_, args) => args.State = CoreWebView2PermissionState.Deny;
        web.DOMContentLoaded += (_, _) => SendInit();
        web.NavigationCompleted += (_, args) =>
        {
            if (!args.IsSuccess)
            {
                if (!_appEverReady) ExitCode = 21;
                HostLog.Write("Local navigation failed: " + args.WebErrorStatus);
                ShowStatus("无法载入本地界面；当前窗口未自动切换到浏览器。", true);
            }
        };
        web.WebMessageReceived += MessageReceived;
        web.ProcessFailed += (_, args) =>
        {
            HostLog.Write("WebView2 process failure: " + args.ProcessFailedKind);
            // GPU/utility/sandbox-helper failures recover automatically and do
            // not imply that the application's main document was lost.
            if (args.ProcessFailedKind is not (CoreWebView2ProcessFailedKind.BrowserProcessExited or CoreWebView2ProcessFailedKind.RenderProcessExited or CoreWebView2ProcessFailedKind.RenderProcessUnresponsive)) return;
            _appReady = false; _view.Enabled = false; _closeTimer.Stop(); _closeRequest = null; _closeConfirmationUnavailable = true;
            ShowStatus("界面进程中断，未确认保存或关闭。窗口已保留，请核对日志和本地备份。", true);
            // An unresponsive renderer may recover. Request a fresh ready
            // handshake without reloading or discarding its editing model.
            if (args.ProcessFailedKind == CoreWebView2ProcessFailedKind.RenderProcessUnresponsive) SendInit();
        };
        web.ContainsFullScreenElementChanged += (_, _) => Fullscreen(web.ContainsFullScreenElement);
        web.AddWebResourceRequestedFilter("*", CoreWebView2WebResourceContext.All);
        web.WebResourceRequested += (_, args) =>
        {
            if (!AllowedResource(args.Request.Uri))
                args.Response = environment.CreateWebResourceResponse(Stream.Null, 403, "Blocked", "Content-Type: text/plain\r\n");
        };
    }

    private bool AllowedResource(string value)
    {
        if (value.StartsWith("data:", StringComparison.OrdinalIgnoreCase)) return true;
        if (value.StartsWith("blob:" + _options.Origin + "/", StringComparison.Ordinal)) return true;
        return Uri.TryCreate(value, UriKind.Absolute, out var uri) && uri.Scheme == Uri.UriSchemeHttp && uri.Host == "127.0.0.1"
            && uri.Port == _options.Url!.Port && string.IsNullOrEmpty(uri.UserInfo);
    }

    private void SendInit()
    {
        try
        {
            if (_view.CoreWebView2 is null || !_options.IsAppAddress(_view.Source?.AbsoluteUri ?? "")) return;
            _view.CoreWebView2.PostWebMessageAsJson(JsonSerializer.Serialize(new
            {
                type = "propernotes-host-init", protocol = 1, nonce = _nonce, instance = _options.Instance, revision = _options.Revision
            }));
        }
        catch (Exception error)
        {
            HostLog.Write("Host initialization message failed: " + error.Message);
            ShowStatus("尚未完成窗口与界面的连接确认，窗口已保留。", true);
        }
    }

    private void MessageReceived(object? sender, CoreWebView2WebMessageReceivedEventArgs args)
    {
        if (!_options.IsAppAddress(args.Source)) return;
        try
        {
            var json = args.WebMessageAsJson;
            if (json.Length > 8192) return;
            using var document = JsonDocument.Parse(json);
            var value = document.RootElement;
            if (value.ValueKind != JsonValueKind.Object || !value.TryGetProperty("protocol", out var protocol) || !protocol.TryGetInt32(out var version) || version != 1) return;
            var type = ReadJsonString(value, "type");
            if (type == "propernotes-app-hello") { SendInit(); return; }
            if (ReadJsonString(value, "nonce") != _nonce) return;
            if (type == "propernotes-app-ready" && ReadJsonString(value, "instance") == _options.Instance && ReadJsonString(value, "revision") == _options.Revision)
            {
                _appReady = true; _appEverReady = true; _closeConfirmationUnavailable = false; _view.Enabled = true; _startupTimer.Stop(); ShowStatus("", false); _view.Focus(); return;
            }
            if (type != "propernotes-close-result" || _closeRequest is null || ReadJsonString(value, "requestId") != _closeRequest || !value.TryGetProperty("allow", out var allow) || allow.ValueKind is not (JsonValueKind.True or JsonValueKind.False)) return;
            _closeTimer.Stop(); _closeRequest = null;
            if (allow.GetBoolean())
            {
                var now = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();
                if (!value.TryGetProperty("expiresAt", out var expiry) || !expiry.TryGetInt64(out var expiresAt) || expiresAt <= now || expiresAt > now + 9000)
                {
                    ShowStatus("关闭确认已过期，当前窗口已保留；请重新关闭。", true); return;
                }
                _allowClose = true; Close();
            }
            else ShowStatus(ReadJsonString(value, "message") is { Length: > 0 } message ? message[..Math.Min(message.Length, 2000)] : "关闭已取消，当前文档仍保留。", true);
        }
        catch (Exception error) when (error is JsonException or InvalidOperationException or ArgumentException)
        {
            HostLog.Write("Rejected host message: " + error.GetType().Name);
        }
    }

    private static string? ReadJsonString(JsonElement value, string key) => value.TryGetProperty(key, out var property) && property.ValueKind == JsonValueKind.String ? property.GetString() : null;

    private void ClosingWindow(object? sender, FormClosingEventArgs args)
    {
        if (_allowClose) return;
        // Before bridge readiness input is disabled and the host has not allowed
        // editing. Canceling startup is different from discarding a ready page.
        if (!_appEverReady) { _allowClose = true; return; }
        args.Cancel = true;
        if (_closeRequest is not null) return;
        if (_closeConfirmationUnavailable)
        {
            var choice = MessageBox.Show(this,
                "界面进程已中断或上次关闭核对超时。\n强制关闭可能丢失未保存的笔迹、文字和其他输入；本地备份不保证包含最新修改。\n\n是否强制关闭窗口？\n选择“否”将保留窗口，并在界面可响应时重试正常关闭核对。",
                HostOptions.Product, MessageBoxButtons.YesNo, MessageBoxIcon.Warning, MessageBoxDefaultButton.Button2);
            if (choice == DialogResult.Yes) { _allowClose = true; args.Cancel = false; return; }
            if (_appReady && _view.CoreWebView2 is not null) _closeConfirmationUnavailable = false;
        }
        if (!_appReady || _view.CoreWebView2 is null)
        {
            ShowStatus("界面无法确认可安全关闭，窗口已保留。未自动放弃文档。", true); return;
        }
        _closeRequest = Guid.NewGuid().ToString("N");
        ShowStatus("正在核对未保存的文档与输入…", true);
        try
        {
            _view.CoreWebView2.PostWebMessageAsJson(JsonSerializer.Serialize(new { type = "propernotes-close-request", protocol = 1, nonce = _nonce, requestId = _closeRequest }));
            _closeTimer.Start();
        }
        catch (Exception error)
        {
            _closeRequest = null; HostLog.Write("Close request failed: " + error.Message);
            ShowStatus("未能确认关闭，当前窗口已保留。", true);
        }
    }

    private void ShowStatus(string message, bool visible) { if (IsDisposed || Disposing) return; _statusText.Text = message; _status.Visible = visible; }
    private void Fullscreen(bool enabled)
    {
        if (IsDisposed || Disposing || enabled == _fullscreen) return;
        _fullscreen = enabled;
        if (enabled)
        {
            _beforeFullscreen = WindowState == FormWindowState.Normal ? Bounds : RestoreBounds;
            _beforeFullscreenState = WindowState;
            WindowState = FormWindowState.Normal; FormBorderStyle = FormBorderStyle.None; Bounds = Screen.FromControl(this).Bounds;
        }
        else
        {
            FormBorderStyle = FormBorderStyle.Sizable; Bounds = _beforeFullscreen; WindowState = _beforeFullscreenState;
        }
    }

    protected override void OnFormClosed(FormClosedEventArgs args)
    {
        _lifetime.Cancel(); _startupTimer.Stop(); _closeTimer.Stop();
        base.OnFormClosed(args);
    }
    protected override void Dispose(bool disposing)
    {
        if (disposing && !_resourcesDisposed)
        {
            _resourcesDisposed = true;
            _lifetime.Cancel(); _startupTimer.Dispose(); _closeTimer.Dispose(); _lifetime.Dispose();
        }
        base.Dispose(disposing);
    }
}
