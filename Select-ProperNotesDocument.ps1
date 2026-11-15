$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
[Console]::InputEncoding = [System.Text.UTF8Encoding]::new($false)
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)
$taskDialog = $null
$taskOwner = $null
$taskExitCode = 0
$taskReply = $null
try {
    $taskRaw = [Console]::In.ReadToEnd()
    if ($taskRaw.Length -gt 65536) { throw 'Invalid dialog request' }
    $taskRequest = $taskRaw | ConvertFrom-Json
    if ($taskRequest.kind -ne 'open' -and $taskRequest.kind -ne 'save') { throw 'Invalid dialog kind' }
    $null = Add-Type -AssemblyName System.Windows.Forms
    [void][System.Windows.Forms.Application]::EnableVisualStyles()
    if ($taskRequest.kind -eq 'open') {
        $taskDialog = New-Object System.Windows.Forms.OpenFileDialog
        $taskDialog.Title = 'Open PDF - ProperNotes for Windows'
        $taskDialog.Multiselect = $false
        $taskDialog.CheckFileExists = $true
    } else {
        $taskDialog = New-Object System.Windows.Forms.SaveFileDialog
        $taskDialog.Title = 'Save PDF - ProperNotes for Windows'
        $taskDialog.OverwritePrompt = $true
        $taskDialog.CheckFileExists = $false
        $taskName = [System.IO.Path]::GetFileName([string]$taskRequest.suggestedName)
        if ([string]::IsNullOrWhiteSpace($taskName)) { $taskName = 'Document.pdf' }
        $taskDialog.FileName = $taskName
    }
    $taskDialog.Filter = 'PDF documents (*.pdf)|*.pdf'
    $taskDialog.DefaultExt = 'pdf'
    $taskDialog.AddExtension = $true
    $taskDialog.CheckPathExists = $true
    $taskDialog.ValidateNames = $true
    $taskDialog.RestoreDirectory = $true
    $taskDialog.DereferenceLinks = $false
    # Only the user's explicit Open/Save action launches this helper. A transparent
    # owner keeps the requested dialog visible without opening a console window.
    $taskOwner = New-Object System.Windows.Forms.Form
    $taskOwner.ShowInTaskbar = $false
    $taskOwner.StartPosition = 'CenterScreen'
    $taskOwner.Width = 1
    $taskOwner.Height = 1
    $taskOwner.Opacity = 0
    $taskOwner.TopMost = $true
    [void]$taskOwner.Show()
    [void]$taskOwner.Activate()
    $taskResult = $taskDialog.ShowDialog($taskOwner)
    if ($taskResult -eq [System.Windows.Forms.DialogResult]::OK) {
        $taskReply = @{ cancelled = $false; path = $taskDialog.FileName }
    } else {
        $taskReply = @{ cancelled = $true; path = $null }
    }
    # This script only returns a selected path; it never opens or writes a PDF.
} catch {
    $taskExitCode = 1
    $taskReply = @{ error = @{ code = 'NATIVE_DIALOG_UNAVAILABLE'; message = 'Windows file dialog is unavailable.' } }
} finally {
    if ($null -ne $taskDialog) { [void]$taskDialog.Dispose() }
    if ($null -ne $taskOwner) { [void]$taskOwner.Close(); [void]$taskOwner.Dispose() }
}
$taskJSON = $taskReply | ConvertTo-Json -Depth 4 -Compress
# Keep the JSON transport ASCII as well: Unicode escape sequences preserve exact
# UTF-16 filenames, including characters outside the BMP, across PS 5.1 pipes.
$taskBuilder = New-Object System.Text.StringBuilder
foreach ($taskCharacter in $taskJSON.ToCharArray()) {
    $taskCode = [int][char]$taskCharacter
    if ($taskCode -gt 127) { [void]$taskBuilder.Append(('\u{0:x4}' -f $taskCode)) }
    else { [void]$taskBuilder.Append($taskCharacter) }
}
[Console]::Out.WriteLine($taskBuilder.ToString())
exit $taskExitCode
