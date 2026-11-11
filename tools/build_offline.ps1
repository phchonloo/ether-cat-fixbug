param([string]$OutputDirectory = '')
$ErrorActionPreference = 'Stop'
function Invoke-ComRetry([scriptblock]$Action) {
    for ($attempt = 0; $attempt -lt 120; $attempt++) {
        try { return (& $Action) } catch {
            if ($_.Exception.ToString() -notmatch '80010001|8001010A|CALL_REJECTED|被呼叫方|null-valued expression') { throw }
            if ($attempt -eq 119) { throw }
            Start-Sleep -Milliseconds 250
        }
    }
}
$package = Split-Path -Parent $PSScriptRoot
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $package ('_build_' + (Get-Date -Format 'yyyyMMdd_HHmmss')) }
if (Test-Path -LiteralPath $OutputDirectory) { throw 'Use a NEW directory; never overwrite an existing solution.' }
$null = New-Item -ItemType Directory -Path $OutputDirectory
$template = 'C:\TwinCAT\3.1\Components\Base\PrjTemplate\TwinCAT Project.tsproj'
$source = Join-Path $package 'PLC\FCM_Homing.plcproj'
$sourceHash = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
$xae = $null
try {
    Write-Output 'OFFLINE ONLY: isolated XAE instance; no activation, login, download, scan or motion.'
    Write-Output 'Creating hidden XAE automation instance...'
    $xae = New-Object -ComObject 'TcXaeShell.DTE.15.0'
    Write-Output 'XAE automation instance created.'
    Invoke-ComRetry { $xae.SuppressUI = $true }
    Invoke-ComRetry { $xae.MainWindow.Visible = $false }
    Invoke-ComRetry {
        $script:solution = $xae.Solution
        if ($null -eq $script:solution) { throw '80010001: isolated XAE solution is still loading.' }
    }
    Write-Output 'Creating isolated offline solution...'
    Invoke-ComRetry { $script:solution.Create($OutputDirectory, 'FCM_Homing_Offline') }
    Invoke-ComRetry { $script:project = $script:solution.AddFromTemplate($template, (Join-Path $OutputDirectory 'System'), 'FCM_Homing_Offline', $false) }
    Invoke-ComRetry {
        if ($null -eq $script:project) { $script:project = $script:solution.Projects.Item(1) }
        $script:system = $script:project.Object
        if ($null -eq $script:system) { throw '80010001: XAE system project still loading.' }
    }
    Invoke-ComRetry { $script:plc = $script:system.LookupTreeItem('TIPC') }
    # Existing-project import: 0=COPY, 1=MOVE, 2=original location. Never move source.
    $null = Invoke-ComRetry { $plc.CreateChild('FCM_Homing', 0, '', $source) }
    Invoke-ComRetry { $script:solution.SaveAs((Join-Path $OutputDirectory 'FCM_Homing_Offline.sln')) }
    Invoke-ComRetry {
        $script:solutionBuild = $script:solution.SolutionBuild
        if ($null -eq $script:solutionBuild) { throw '80010001: isolated XAE solution build service is still loading.' }
    }
    Write-Output 'Building imported PLC source...'
    Invoke-ComRetry { $script:solutionBuild.Build($true) }
    $buildInfo = $script:solutionBuild.LastBuildInfo
    Write-Output ('LastBuildInfo=' + $buildInfo)
    # TcXaeShell may expose DTE only. Access the standard windows without DTE2.
    $errors = $null
    try {
        Invoke-ComRetry { $script:errors = $xae.Windows.Item('{D78612C7-9962-4B83-95D9-268046DAD23A}').Object.ErrorItems }
        $errors = $script:errors
    } catch { Write-Warning $_ }
    $errorCount = 0
    for ($n = 1; $n -le $errors.Count; $n++) {
        $item = $errors.Item($n)
        Write-Output ('[{0}] {1}:{2}: {3}' -f $item.ErrorLevel, $item.FileName, $item.Line, $item.Description)
        if ($item.ErrorLevel -eq 2) { $errorCount++ }
    }
    $panes = $null
    try {
        Invoke-ComRetry { $script:panes = $xae.Windows.Item('{34E76E81-EE4A-11D0-AE2E-00A0C90FFFC3}').Object.OutputWindowPanes }
        $panes = $script:panes
    } catch { Write-Warning $_ }
    for ($n = 1; $n -le $panes.Count; $n++) {
        $pane = $panes.Item($n)
        $editPoint = $pane.TextDocument.StartPoint.CreateEditPoint()
        Write-Output $editPoint.GetText($pane.TextDocument.EndPoint)
    }
    if ($buildInfo -ne 0) { throw 'Offline build failed; see compiler diagnostics.' }
    $compiled = Get-ChildItem -LiteralPath $OutputDirectory -Filter '*.tmc' -Recurse
    if (-not $compiled) { throw 'No generated TMC: build success is NOT confirmed.' }
    Write-Output ('OFFLINE_BUILD_OK: ' + $OutputDirectory)
} finally {
    if ($null -ne $xae) {
        try { Invoke-ComRetry { $xae.Quit() } } catch { Write-Warning ('Cannot close isolated XAE: ' + $_.Exception.Message) }
    }
    if (-not (Test-Path -LiteralPath $source)) { throw 'Source project missing after import.' }
    if ((Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash -ne $sourceHash) { throw 'Source project unexpectedly changed during offline build.' }
}
