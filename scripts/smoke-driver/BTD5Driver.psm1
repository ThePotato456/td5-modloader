# Drives the copied BTD5 game window for live-smoke tests: mouse input, screen
# recognition, runtime-log waits, and named game actions. Used by
# scripts/drive-smoke-match.ps1.

Set-StrictMode -Version Latest

Add-Type -AssemblyName System.Drawing
Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @'
using System;
using System.Drawing;
using System.Runtime.InteropServices;
public static class SmokeInput {
    [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    [StructLayout(LayoutKind.Sequential)] struct LASTINPUTINFO { public uint cbSize, dwTime; }
    [DllImport("user32.dll")] static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] static extern bool BringWindowToTop(IntPtr hWnd);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr hWnd, IntPtr pid);
    [DllImport("user32.dll")] static extern bool AttachThreadInput(uint from, uint to, bool attach);
    [DllImport("kernel32.dll")] static extern uint GetCurrentThreadId();
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr hWnd, out RECT r);
    [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr hWnd, ref POINT p);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] static extern void mouse_event(uint flags, uint dx, uint dy, uint data, UIntPtr extra);
    [DllImport("user32.dll")] static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
    [DllImport("user32.dll")] static extern bool GetLastInputInfo(ref LASTINPUTINFO info);
    // A background process may not take the foreground directly; sharing the
    // current foreground thread's input state lets SetForegroundWindow succeed.
    public static bool Focus(IntPtr window) {
        if (GetForegroundWindow() == window) return true;
        uint self = GetCurrentThreadId();
        uint current = GetWindowThreadProcessId(GetForegroundWindow(), IntPtr.Zero);
        bool attached = current != 0 && current != self && AttachThreadInput(self, current, true);
        try { BringWindowToTop(window); SetForegroundWindow(window); }
        finally { if (attached) AttachThreadInput(self, current, false); }
        return GetForegroundWindow() == window;
    }
    public static void LeftClick() {
        mouse_event(0x0002, 0, 0, 0, UIntPtr.Zero);
        System.Threading.Thread.Sleep(60);
        mouse_event(0x0004, 0, 0, 0, UIntPtr.Zero);
    }
    public static void Key(byte vk) {
        keybd_event(vk, 0, 0, UIntPtr.Zero);
        System.Threading.Thread.Sleep(60);
        keybd_event(vk, 0, 2, UIntPtr.Zero);
    }
    public static double IdleSeconds() {
        var info = new LASTINPUTINFO();
        info.cbSize = (uint)Marshal.SizeOf(info);
        GetLastInputInfo(ref info);
        return unchecked((uint)Environment.TickCount - info.dwTime) / 1000.0;
    }
}
public static class ScreenSig {
    public const int GX = 12, GY = 8;
    public static int[] Of(Bitmap b) {
        var o = new int[GX * GY * 3]; int i = 0;
        for (int gy = 0; gy < GY; gy++) for (int gx = 0; gx < GX; gx++) {
            long r = 0, g = 0, bl = 0, n = 0;
            int x0 = (int)((gx + 0.15) * b.Width / GX), x1 = (int)((gx + 0.85) * b.Width / GX);
            int y0 = (int)((gy + 0.15) * b.Height / GY), y1 = (int)((gy + 0.85) * b.Height / GY);
            int step = Math.Max(1, b.Width / 240);
            for (int y = y0; y < y1; y += step) for (int x = x0; x < x1; x += step) {
                var c = b.GetPixel(x, y); r += c.R; g += c.G; bl += c.B; n++;
            }
            o[i++] = (int)(r / n); o[i++] = (int)(g / n); o[i++] = (int)(bl / n);
        }
        return o;
    }
    public static double Dist(int[] a, int[] b) {
        double d = 0; for (int i = 0; i < a.Length; i++) d += Math.Abs(a[i] - b[i]); return d / a.Length;
    }
}
'@

# Click points measured on a 1904x1017 client area, scaled to the live window.
$script:ReferenceWidth = 1904.0
$script:ReferenceHeight = 1017.0
$script:Points = @{
    MenuPlay       = 950, 474
    NewGame        = 478, 370
    ResumeGame     = 1426, 370
    TrackNext      = 1720, 506
    TrackSelect    = 952, 500
    Easy           = 516, 350
    TenToggle      = 1100, 636
    ModePlay       = 1388, 636
    OverwriteYes   = 1152, 616
    DismissTip     = 860, 730
    StartRound     = 1490, 903
    SettingsGear   = 1580, 903
    PauseResume    = 526, 360
    PauseHome      = 1160, 544
    DialogYes      = 1152, 616
    SellTower      = 714, 930
    UpgradePath1   = 1028, 930
    UpgradePath2   = 1346, 930
    DialogClose    = 442, 166
    PromoNoThanks  = 740, 772
    ShopScrollUp   = 1546, 144
    ShopScrollDown = 1544, 686
}
# The tower panel's scrolling list (between its arrows), reference space.
$script:ShopPanel = @{ X = 1452; Y = 170; Width = 190; Height = 490 }
# Shop slots in the right-hand tower panel, and free grass spots on Monkey
# Lane that reach the track. Later spots are used for additional towers.
# 'second' and 'third' are the left column's second and third rows, where
# further custom towers that lead the shop land.
$script:ShopSlots = @{ dart = 1500, 210; second = 1500, 290; third = 1500, 370 }
$script:PlacementSpots = @((620, 260), (1100, 300), (760, 640))
$script:EmptyGrass = @(1000, 160)

# Which point to click on each recognised screen. 'ModeChoice' on the Easy
# screen toggles Ten mode once when requested, then starts the match.
$script:MenuActions = @{
    MainMenu = 'MenuPlay'; ChooseGame = 'NewGame'; TrackEvent = 'TrackNext'; TrackBeginner = 'TrackSelect'
    ModeSelect = 'Easy'; EasyDetail = 'ModeChoice'; TenSelected = 'ModeChoice'
    Overwrite = 'OverwriteYes'; Bloonvasion = 'DialogClose'; Promo = 'PromoNoThanks'
    GoldenBloon = 'DialogClose'
}
$script:ExitActions = @{
    Match = 'SettingsGear'; MatchTip = 'SettingsGear'; MatchSelected = 'SettingsGear'
    PauseMenu = 'PauseHome'; SaveExit = 'DialogYes'; GameOver = 'DialogClose'; Promo = 'PromoNoThanks'
    GoldenBloon = 'DialogClose'
}

$script:lastSeen = ''
$script:LastInputAt = $null
$script:State = @{
    LogPath = $null; ShotDirectory = $null; ShotIndex = 0; Harness = $null
    Signatures = @{}; Towers = [System.Collections.Generic.List[object]]::new()
}

function Initialize-Driver {
    param([string]$LogPath, [string]$ShotDirectory, [Diagnostics.Process]$Harness)
    $script:State.LogPath = $LogPath
    $script:State.ShotDirectory = $ShotDirectory
    $script:State.Harness = $Harness
    $script:State.ShotIndex = 0
    $script:State.Towers.Clear()
    $script:State.Signatures = @{}
    (Get-Content (Join-Path $PSScriptRoot 'screens.json') -Raw | ConvertFrom-Json).PSObject.Properties |
        ForEach-Object { $script:State.Signatures[$_.Name] = [int[]]$_.Value }
}

function Get-IdleTime { [SmokeInput]::IdleSeconds() }

function Test-HarnessExited {
    $harness = $script:State.Harness
    return $null -ne $harness -and $harness.HasExited
}

function Get-RuntimeLog {
    try {
        $stream = [IO.File]::Open($script:State.LogPath, 'Open', 'Read', 'ReadWrite, Delete')
        try { return (New-Object IO.StreamReader($stream)).ReadToEnd() } finally { $stream.Dispose() }
    }
    catch { return '' }
}

function Get-LogCount([string]$text) {
    $log = Get-RuntimeLog
    $count = 0; $index = 0
    while (($index = $log.IndexOf($text, $index, [StringComparison]::Ordinal)) -ge 0) { $count++; $index += $text.Length }
    return $count
}

# Waits until the runtime log contains $text at least $count times.
function Wait-Log([string]$text, [int]$timeoutSeconds = 30, [int]$count = 1) {
    $deadline = (Get-Date).AddSeconds($timeoutSeconds)
    while ((Get-Date) -lt $deadline) {
        if ((Get-LogCount $text) -ge $count) { return $true }
        if (Test-HarnessExited) { return $false }
        Start-Sleep -Milliseconds 400
    }
    return $false
}

function Get-GameWindow {
    $game = Get-Process BTD5-Win -ErrorAction SilentlyContinue | Where-Object MainWindowHandle -ne 0 |
        Select-Object -First 1
    if ($game) { return $game.MainWindowHandle }
    return [IntPtr]::Zero
}

function Get-ClientGeometry {
    $window = Get-GameWindow
    if ($window -eq [IntPtr]::Zero) { return $null }
    $client = New-Object SmokeInput+RECT
    [SmokeInput]::GetClientRect($window, [ref]$client) | Out-Null
    $origin = New-Object SmokeInput+POINT
    [SmokeInput]::ClientToScreen($window, [ref]$origin) | Out-Null
    return [pscustomobject]@{ Window = $window; X = $origin.X; Y = $origin.Y; Width = $client.Right; Height = $client.Bottom }
}

function Get-ClientBitmap {
    $geometry = Get-ClientGeometry
    if ($null -eq $geometry -or $geometry.Width -le 0) { return $null }
    if ([SmokeInput]::GetForegroundWindow() -ne $geometry.Window) { [SmokeInput]::Focus($geometry.Window) | Out-Null }
    $bitmap = New-Object Drawing.Bitmap $geometry.Width, $geometry.Height
    $graphics = [Drawing.Graphics]::FromImage($bitmap)
    try { $graphics.CopyFromScreen($geometry.X, $geometry.Y, 0, 0, $bitmap.Size) } finally { $graphics.Dispose() }
    return $bitmap
}

# Names the current screen by its nearest fingerprint in screens.json, or
# 'Unknown' while loading or animating. Same-screen frames score about 1-2.
function Get-ScreenName {
    $bitmap = Get-ClientBitmap
    if ($null -eq $bitmap) { return 'NoWindow' }
    try { $signature = [ScreenSig]::Of($bitmap) } finally { $bitmap.Dispose() }
    $best = 'Unknown'; $bestDistance = 3.5
    foreach ($name in $script:State.Signatures.Keys) {
        $distance = [ScreenSig]::Dist($signature, $script:State.Signatures[$name])
        if ($distance -lt $bestDistance) { $best = $name; $bestDistance = $distance }
    }
    return $best
}

# Captures the live game window and stores its fingerprint in screens.json
# under $Name, replacing any existing entry. Use it when a screen changes, for
# example when a new special event replaces the carousel's first page.
function Update-ScreenSignature {
    [CmdletBinding(SupportsShouldProcess)]
    param([Parameter(Mandatory)][string]$Name)
    $bitmap = Get-ClientBitmap
    if ($null -eq $bitmap) { throw 'The game window is not open.' }
    try { $signature = [ScreenSig]::Of($bitmap) } finally { $bitmap.Dispose() }
    $path = Join-Path $PSScriptRoot 'screens.json'
    if (-not $PSCmdlet.ShouldProcess($path, "store fingerprint '$Name'")) { return }
    $screens = [ordered]@{}
    (Get-Content $path -Raw | ConvertFrom-Json).PSObject.Properties | ForEach-Object { $screens[$_.Name] = $_.Value }
    $screens[$Name] = $signature
    $screens | ConvertTo-Json -Compress | Set-Content -Encoding ascii $path
    $script:State.Signatures[$Name] = [int[]]$signature
    Write-Host "stored fingerprint '$Name' in $path"
}

# Saves a half-size frame of the game window when a shot directory is set.
function Save-GameShot([string]$label) {
    if (-not $script:State.ShotDirectory) { return }
    $bitmap = Get-ClientBitmap
    if ($null -eq $bitmap) { return }
    $half = New-Object Drawing.Bitmap ([int]($bitmap.Width / 2)), ([int]($bitmap.Height / 2))
    try {
        $graphics = [Drawing.Graphics]::FromImage($half)
        try { $graphics.DrawImage($bitmap, 0, 0, $half.Width, $half.Height) } finally { $graphics.Dispose() }
        $script:State.ShotIndex++
        New-Item -ItemType Directory -Path $script:State.ShotDirectory -Force | Out-Null
        $path = Join-Path $script:State.ShotDirectory ('{0:D2}-{1}.png' -f $script:State.ShotIndex, $label)
        $half.Save($path, [Drawing.Imaging.ImageFormat]::Png)
        Write-Host "shot $path"
    }
    finally { $half.Dispose(); $bitmap.Dispose() }
}

# Clicks a named point or an explicit reference-space (x, y) pair. Input goes
# only to the game window; the click is skipped if it cannot be focused.
function Invoke-GameClick {
    param([Parameter(Mandatory)] $Target, [int]$AfterMs = 800, [string]$Label)
    if ($Target -is [string]) { $reference = $script:Points[$Target]; if (-not $Label) { $Label = $Target } }
    else { $reference = $Target; if (-not $Label) { $Label = "click-$($Target[0])-$($Target[1])" } }
    if ($null -eq $reference) { throw "Unknown click point '$Target'." }
    # Stop if anyone used the mouse or keyboard since the driver's last click.
    if ($null -ne $script:LastInputAt) {
        $sinceOwnInput = ((Get-Date) - $script:LastInputAt).TotalSeconds
        if ([SmokeInput]::IdleSeconds() + 1 -lt $sinceOwnInput) {
            throw 'Someone used the mouse or keyboard during the run; stopping so input is not taken over.'
        }
    }
    $geometry = Get-ClientGeometry
    if ($null -eq $geometry) { return $false }
    [SmokeInput]::Focus($geometry.Window) | Out-Null
    Start-Sleep -Milliseconds 150
    if ([SmokeInput]::GetForegroundWindow() -ne $geometry.Window) {
        Write-Warning "Skipped $Label because the game window is not in the foreground."
        return $false
    }
    $x = $geometry.X + [int]($reference[0] / $script:ReferenceWidth * $geometry.Width)
    $y = $geometry.Y + [int]($reference[1] / $script:ReferenceHeight * $geometry.Height)
    [SmokeInput]::SetCursorPos($x, $y) | Out-Null
    Start-Sleep -Milliseconds 120
    [SmokeInput]::LeftClick()
    $script:LastInputAt = Get-Date
    Write-Host "click $Label"
    Start-Sleep -Milliseconds $AfterMs
    Save-GameShot $Label
    return $true
}

function Send-GameKey([byte]$virtualKey, [string]$label) {
    $geometry = Get-ClientGeometry
    if ($null -eq $geometry -or -not [SmokeInput]::Focus($geometry.Window)) { return $false }
    [SmokeInput]::Key($virtualKey)
    $script:LastInputAt = Get-Date
    Write-Host "key $label"
    Start-Sleep -Milliseconds 500
    return $true
}

# Clicks each recognised screen's button until the runtime logs $Goal (at
# least $GoalCount times). A click the game ignored is repeated once the
# screen has stayed the same for 4 s. After NewGame or TrackNext, an
# unrecognised carousel page (a different special event) is stepped past.
# Otherwise a screen that stays unrecognised for 5 s gets $UnknownTarget.
function Invoke-Navigation {
    param([hashtable]$Actions, [string]$Goal, [int]$GoalCount = 1, [int]$TimeoutSeconds = 120,
        [string]$UnknownTarget)
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    $lastScreen = ''; $lastTarget = ''; $lastClick = [DateTime]::MinValue; $unknownSince = $null
    while ((Get-Date) -lt $deadline) {
        if ((Get-LogCount $Goal) -ge $GoalCount) { return $true }
        if (Test-HarnessExited) { return $false }
        $screen = Get-ScreenName
        if ($screen -ne $script:lastSeen) {
            Write-Host ("{0:HH:mm:ss.f} seen {1}" -f (Get-Date), $screen)
            $script:lastSeen = $screen
        }
        $target = $null
        if ($Actions.ContainsKey($screen)) {
            $unknownSince = $null
            if ($screen -ne $lastScreen -or ((Get-Date) - $lastClick).TotalSeconds -gt 4) {
                $target = $Actions[$screen]
                if ($target -is [scriptblock]) { $target = & $target }
            }
        }
        elseif ($screen -eq 'Unknown' -and $lastTarget -in 'NewGame', 'TrackNext') {
            if ($null -eq $unknownSince) { $unknownSince = Get-Date }
            elseif (((Get-Date) - $unknownSince).TotalSeconds -gt 3) { $target = 'TrackNext'; $unknownSince = $null }
        }
        elseif ($screen -eq 'Unknown' -and $UnknownTarget) {
            if ($null -eq $unknownSince) { $unknownSince = Get-Date }
            elseif (((Get-Date) - $unknownSince).TotalSeconds -gt 5) { $target = $UnknownTarget; $unknownSince = $null }
        }
        if ($target) {
            # Keep the frame that triggered a fallback so stale fingerprints can be refreshed.
            if ($screen -eq 'Unknown') { Save-GameShot "unknown-before-$target" }
            Write-Host "screen $screen"
            Invoke-GameClick $target -AfterMs 1000 | Out-Null
            $lastScreen = $screen; $lastTarget = $target; $lastClick = Get-Date
        }
        else { Start-Sleep -Milliseconds 500 }
    }
    return $false
}

# --- Game actions. Each returns $true when the runtime log confirms it. ---

# From the main menu or any menu screen: Monkey Lane, Easy, optionally Ten
# mode (starts at round 10 with multi-layer bloons), then the match. -Resume
# continues the saved match instead and keeps the towers already known.
function Enter-Match {
    param([switch]$Ten, [switch]$Resume, [int]$TimeoutSeconds = 120)
    $already = Get-LogCount 'Lifecycle Sample observed match.started'
    $script:tenPending = [bool]$Ten
    $chooseMode = { if ($script:tenPending) { $script:tenPending = $false; 'TenToggle' } else { 'ModePlay' } }
    $actions = @{}
    foreach ($screen in $script:MenuActions.Keys) {
        $actions[$screen] = if ($script:MenuActions[$screen] -eq 'ModeChoice') { $chooseMode } else { $script:MenuActions[$screen] }
    }
    if ($Resume) { $actions['ChooseGame'] = 'ResumeGame' }
    if (-not (Invoke-Navigation $actions 'Lifecycle Sample observed match.started' ($already + 1) $TimeoutSeconds)) {
        return $false
    }
    if (-not $Resume) { $script:State.Towers.Clear() }
    Start-Sleep -Seconds 3
    Invoke-GameClick DismissTip -AfterMs 600 | Out-Null
    return $true
}

# Buys a tower from the shop and places it on the next free spot. Returns the
# placed tower's id, or $null when no placement was logged (for example when
# Lua cancelled the purchase).
function Add-Tower {
    param([string]$Kind = 'dart', [int]$TimeoutSeconds = 6)
    $slot = $script:ShopSlots[$Kind]
    if ($null -eq $slot) { throw "Unknown tower kind '$Kind'." }
    $spot = $script:PlacementSpots[[Math]::Min($script:State.Towers.Count, $script:PlacementSpots.Count - 1)]
    $before = Get-LogCount 'Lifecycle Sample observed tower.placed id='
    Invoke-GameClick $slot -AfterMs 500 -Label "shop-$Kind" | Out-Null
    Invoke-GameClick $spot -AfterMs 700 -Label "place-$Kind" | Out-Null
    if (-not (Wait-Log 'Lifecycle Sample observed tower.placed id=' $TimeoutSeconds ($before + 1))) {
        # A rejected purchase leaves nothing on the cursor (Escape would open
        # the pause menu), so only make sure the match is not paused.
        if ((Get-ScreenName) -eq 'PauseMenu') { Invoke-GameClick PauseResume -AfterMs 800 | Out-Null }
        return $null
    }
    $ids = [regex]::Matches((Get-RuntimeLog), 'Lifecycle Sample observed tower\.placed id=(\d+)')
    $id = $ids[$ids.Count - 1].Groups[1].Value
    $script:State.Towers.Add([pscustomobject]@{ Id = $id; Spot = $spot })
    return $id
}

# Clicking a selected tower deselects it, and a new tower starts selected, so
# clear any selection on empty grass before clicking the tower.
function Select-Tower([int]$index = 0) {
    if ($script:State.Towers.Count -le $index) { throw 'No placed tower to select.' }
    Invoke-GameClick $script:EmptyGrass -AfterMs 500 -Label 'deselect' | Out-Null
    Invoke-GameClick $script:State.Towers[$index].Spot -AfterMs 700 -Label 'select-tower' | Out-Null
}

# Attempts an upgrade on the selected tower's path; logs show whether Lua
# cancelled it. Returns $true when tower.upgrading was logged.
function Invoke-Upgrade([int]$path = 1, [int]$index = 0) {
    Select-Tower $index
    $before = Get-LogCount 'Lifecycle Sample observed tower.upgrading'
    $beforeCancel = Get-LogCount 'Lifecycle Sample cancelled tower.upgrading'
    Invoke-GameClick ("UpgradePath$path") -AfterMs 800 | Out-Null
    $deadline = (Get-Date).AddSeconds(5)
    while ((Get-Date) -lt $deadline) {
        if ((Get-LogCount 'Lifecycle Sample observed tower.upgrading') -gt $before -or
            (Get-LogCount 'Lifecycle Sample cancelled tower.upgrading') -gt $beforeCancel) { return $true }
        Start-Sleep -Milliseconds 300
    }
    return $false
}

# Attempts to sell the selected tower. With -ExpectCancelled it succeeds only
# when Lua cancelled the sale, and the tower stays placed.
function Invoke-Sell([int]$index = 0, [switch]$ExpectCancelled) {
    Select-Tower $index
    # Lua logs "observed tower.selling" before cancelling, so only
    # tower.sold proves the tower is gone.
    $before = Get-LogCount 'Lifecycle Sample observed tower.sold id='
    $beforeCancel = Get-LogCount 'Lifecycle Sample cancelled tower.selling'
    Invoke-GameClick SellTower -AfterMs 800 | Out-Null
    $deadline = (Get-Date).AddSeconds(5)
    while ((Get-Date) -lt $deadline) {
        if ((Get-LogCount 'Lifecycle Sample cancelled tower.selling') -gt $beforeCancel) { return $true }
        if ((Get-LogCount 'Lifecycle Sample observed tower.sold id=') -gt $before) {
            $script:State.Towers.RemoveAt($index)
            return -not $ExpectCancelled
        }
        Start-Sleep -Milliseconds 300
    }
    return $false
}

# Starts the next round and turns on fast-forward, then waits for it to end
# (or for the harness to finish). Returns $false once lives reach zero.
function Invoke-Round {
    param([int]$TimeoutSeconds = 90)
    $ended = Get-LogCount 'Lifecycle Sample observed round.ended'
    Invoke-GameClick StartRound -AfterMs 400 | Out-Null
    Invoke-GameClick StartRound -AfterMs 0 -Label 'FastForward' | Out-Null
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while ((Get-Date) -lt $deadline -and -not (Test-HarnessExited)) {
        if ((Get-LogCount 'Lifecycle Sample observed round.ended') -gt $ended) { return $true }
        if ((Get-RuntimeLog) -match 'observed lives\.changed \d+->0"') { return $false }
        Start-Sleep -Milliseconds 500
    }
    return -not (Test-HarnessExited)
}

# Fingerprint of the tower panel's scrolling list, to tell whether it moved.
function Get-ShopPanelSignature {
    $bitmap = Get-ClientBitmap
    if ($null -eq $bitmap) { return $null }
    try {
        $p = $script:ShopPanel
        $rect = New-Object Drawing.Rectangle ([int]($p.X / $script:ReferenceWidth * $bitmap.Width)),
            ([int]($p.Y / $script:ReferenceHeight * $bitmap.Height)),
            ([int]($p.Width / $script:ReferenceWidth * $bitmap.Width)),
            ([int]($p.Height / $script:ReferenceHeight * $bitmap.Height))
        $crop = $bitmap.Clone($rect, $bitmap.PixelFormat)
        try { return [ScreenSig]::Of($crop) } finally { $crop.Dispose() }
    }
    finally { $bitmap.Dispose() }
}

# Clicks the tower panel's scroll arrow up to $Count times, confirming after
# each click that the list moved. Returns the number of confirmed moves; it
# stops early once a click no longer changes the list (the end is reached).
function Invoke-ShopScroll {
    param([ValidateSet('up', 'down')][string]$Direction = 'down', [int]$Count = 20)
    $target = if ($Direction -eq 'up') { 'ShopScrollUp' } else { 'ShopScrollDown' }
    $moved = 0
    $before = Get-ShopPanelSignature
    for ($i = 1; $i -le $Count; $i++) {
        Invoke-GameClick $target -AfterMs 900 -Label "shop-$Direction-$i" | Out-Null
        $after = Get-ShopPanelSignature
        if ($null -eq $before -or $null -eq $after) { break }
        $change = [ScreenSig]::Dist($before, $after)
        Write-Host ("shop scroll {0} {1}: list change {2:N2}" -f $Direction, $i, $change)
        if ($change -lt 0.5) { break }
        $moved++
        $before = $after
    }
    return $moved
}

# Leaves the match for the main menu (game over dialog or pause menu) and
# waits for match.ended.
function Exit-Match {
    param([int]$TimeoutSeconds = 60)
    $already = Get-LogCount 'Lifecycle Sample observed match.ended'
    # In-match frames vary with bloons and towers, so an unrecognised frame
    # during the exit is treated as the match and opens the pause menu.
    Invoke-GameClick SettingsGear -AfterMs 1200 | Out-Null
    $ended = Invoke-Navigation $script:ExitActions 'Lifecycle Sample observed match.ended' ($already + 1) $TimeoutSeconds `
        -UnknownTarget SettingsGear
    if ($ended) { Start-Sleep -Seconds 2 }
    return $ended
}

# --- Dry run ---

# Checks the driver's own tables: every action names a known click point and
# every recognised screen has a fingerprint. Needs Initialize-Driver first.
function Test-DriverTable {
    $problems = [System.Collections.Generic.List[string]]::new()
    $special = @('ModeChoice')
    foreach ($table in @(@{ Name = 'menu'; Map = $script:MenuActions }, @{ Name = 'exit'; Map = $script:ExitActions })) {
        foreach ($screen in $table.Map.Keys) {
            if (-not $script:State.Signatures.ContainsKey($screen)) {
                $problems.Add("$($table.Name) action screen '$screen' has no fingerprint in screens.json")
            }
            $point = $table.Map[$screen]
            if ($point -notin $special -and -not $script:Points.ContainsKey($point)) {
                $problems.Add("$($table.Name) action '$screen' clicks unknown point '$point'")
            }
        }
    }
    foreach ($point in 'TenToggle', 'ModePlay', 'DismissTip', 'StartRound', 'SellTower', 'UpgradePath1',
        'UpgradePath2', 'PauseResume') {
        if (-not $script:Points.ContainsKey($point)) { $problems.Add("required point '$point' is missing") }
    }
    foreach ($name in $script:Points.Keys) {
        $p = $script:Points[$name]
        if ($p.Count -ne 2 -or $p[0] -lt 0 -or $p[0] -ge $script:ReferenceWidth -or $p[1] -lt 0 -or
            $p[1] -ge $script:ReferenceHeight) {
            $problems.Add("point '$name' ($($p -join ',')) is outside the ${script:ReferenceWidth}x${script:ReferenceHeight} reference area")
        }
    }
    return , $problems
}

# Validates steps and lists the clicks and waits each would perform, without
# touching the game. Returns @{ Actions; Errors }.
function Get-StepPlan {
    param([string[]]$Steps)
    $actions = [System.Collections.Generic.List[object]]::new()
    $errors = [System.Collections.Generic.List[string]]::new()
    foreach ($problem in (Test-DriverTable)) { $errors.Add($problem) }
    $towers = 0; $inMatch = $false
    function Add-Action([string]$step, [string]$action, $target, [string]$expect) {
        $reference = if ($target -is [string] -and $script:Points.ContainsKey($target)) { $script:Points[$target] }
            elseif ($target -is [array]) { $target } else { $null }
        $actions.Add([pscustomobject]@{
            Step = $step; Action = $action
            Target = $(if ($target -is [array]) { '' } else { [string]$target })
            Reference = $(if ($reference) { '{0},{1}' -f $reference[0], $reference[1] } else { '' })
            Expect = $expect
        })
    }
    foreach ($step in $Steps) {
        $parts = @($step -split ':')
        $name = $parts[0]
        $arg = if ($parts.Count -gt 1) { $parts[1] } else { $null }
        switch ($name) {
            'enter' {
                if ($inMatch) { $errors.Add("step '$step': already in a match; add 'exit' first") }
                if ($null -ne $arg -and $arg -notin 'ten', 'resume') { $errors.Add("step '$step': options are 'ten' and 'resume'") }
                Add-Action $step 'navigate' 'menus' ('screens: ' + (($script:MenuActions.GetEnumerator() |
                    Sort-Object Key | ForEach-Object { "$($_.Key)->$($_.Value)" }) -join ' '))
                if ($arg -eq 'resume') {
                    Add-Action $step 'click on ChooseGame' 'ResumeGame' 'log: match.started (saved match)'
                } else {
                    if ($arg -eq 'ten') { Add-Action $step 'click once on Easy' 'TenToggle' 'Ten mode on' }
                    Add-Action $step 'click on Easy' 'ModePlay' 'log: match.started'
                    $towers = 0
                }
                Add-Action $step 'click' 'DismissTip' ''
                $inMatch = $true
            }
            'tower' {
                if (-not $inMatch) { $errors.Add("step '$step': needs a match; add 'enter' first") }
                $kind = if ($arg) { $arg } else { 'dart' }
                if (-not $script:ShopSlots.ContainsKey($kind)) {
                    $errors.Add("step '$step': unknown tower kind '$kind' (known: $($script:ShopSlots.Keys -join ', '))")
                    continue
                }
                $cancelled = $parts.Count -gt 2
                if ($cancelled -and $parts[2] -ne 'cancelled') { $errors.Add("step '$step': third part must be 'cancelled'") }
                $spot = $script:PlacementSpots[[Math]::Min($towers, $script:PlacementSpots.Count - 1)]
                Add-Action $step "click shop slot ($kind)" $script:ShopSlots[$kind] ''
                Add-Action $step "click placement spot $($towers + 1)" $spot $(if ($cancelled) {
                    'no tower.placed within 6 s (Lua cancels the purchase)' } else { 'log: tower.placed' })
                if (-not $cancelled) { $towers++ }
            }
            { $_ -in 'upgrade', 'sell' } {
                if (-not $inMatch -or $towers -eq 0) { $errors.Add("step '$step': no placed tower to $name") }
                if ($name -eq 'upgrade' -and $null -ne $arg -and $arg -notin '1', '2') {
                    $errors.Add("step '$step': path must be 1 or 2")
                }
                if ($name -eq 'sell' -and $null -ne $arg -and $arg -ne 'cancelled') {
                    $errors.Add("step '$step': the only option is 'cancelled'")
                }
                Add-Action $step 'deselect' $script:EmptyGrass ''
                Add-Action $step 'select tower 1' $script:PlacementSpots[0] ''
                if ($name -eq 'upgrade') {
                    Add-Action $step 'click' "UpgradePath$(if ($arg) { $arg } else { 1 })" 'log: tower.upgrading (or cancelled)'
                }
                else {
                    if ($arg -eq 'cancelled') {
                        Add-Action $step 'click' 'SellTower' 'log: Lua cancels tower.selling; the tower stays'
                    }
                    else {
                        Add-Action $step 'click' 'SellTower' 'log: tower.selling (or cancelled)'
                        if ($towers -gt 0) { $towers-- }
                    }
                }
            }
            { $_ -in 'round', 'rounds' } {
                if (-not $inMatch) { $errors.Add("step '$step': needs a match; add 'enter' first") }
                $count = 1
                if ($name -eq 'round' -and $null -ne $arg -and -not [int]::TryParse($arg, [ref]$count)) {
                    $errors.Add("step '$step': count must be a whole number")
                }
                elseif ($name -eq 'round' -and $count -lt 1) { $errors.Add("step '$step': count must be at least 1") }
                $repeat = if ($name -eq 'rounds') { 'repeat until the harness finishes or lives reach 0' } else { "x$count" }
                Add-Action $step "start round ($repeat)" 'StartRound' ''
                Add-Action $step 'fast-forward' 'StartRound' 'log: round.ended'
            }
            'exit' {
                if (-not $inMatch) { $errors.Add("step '$step': not in a match") }
                Add-Action $step 'click' 'SettingsGear' ''
                Add-Action $step 'navigate' 'pause menu' ('screens: ' + (($script:ExitActions.GetEnumerator() |
                    Sort-Object Key | ForEach-Object { "$($_.Key)->$($_.Value)" }) -join ' ') + '; log: match.ended')
                # Exiting saves the match; 'enter:resume' brings its towers back.
                $inMatch = $false
            }
            'wait' {
                $seconds = 0
                if ($null -eq $arg -or -not [int]::TryParse($arg, [ref]$seconds) -or $seconds -lt 1) {
                    $errors.Add("step '$step': use wait:<seconds>")
                }
                Add-Action $step "wait $arg s" '' ''
            }
            'scroll' {
                if (-not $inMatch) { $errors.Add("step '$step': needs a match; add 'enter' first") }
                $direction = if ($arg) { $arg } else { 'down' }
                if ($direction -notin 'up', 'down') { $errors.Add("step '$step': direction must be 'up' or 'down'") }
                $target = if ($direction -eq 'up') { 'ShopScrollUp' } else { 'ShopScrollDown' }
                Add-Action $step "click until the list stops moving" $target 'tower list changes after each click'
            }
            'shot' {
                if (-not $arg -or $arg -match '[\\/:*?"<>|]') { $errors.Add("step '$step': use shot:<label> without path characters") }
                Add-Action $step 'save screenshot' '' $arg
            }
            default { $errors.Add("step '$step': unknown step '$name'") }
        }
    }
    return @{ Actions = $actions; Errors = $errors }
}

Export-ModuleMember -Function Initialize-Driver, Get-IdleTime, Test-HarnessExited, Get-RuntimeLog,
    Get-LogCount, Wait-Log, Get-ScreenName, Update-ScreenSignature, Save-GameShot, Invoke-GameClick, Send-GameKey,
    Invoke-Navigation, Enter-Match, Add-Tower, Select-Tower, Invoke-Upgrade, Invoke-Sell, Invoke-Round,
    Invoke-ShopScroll, Exit-Match, Test-DriverTable, Get-StepPlan
