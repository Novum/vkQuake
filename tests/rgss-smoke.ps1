# Run a Debug build against owned Quake data, with Vulkan synchronization validation.
# Each run uses a fresh directory under build; it never modifies the installed game.
param(
    [Parameter(Mandatory = $true)][string]$GameDirectory,
    [string]$Executable,
    [string]$VulkanSdk = $env:VULKAN_SDK
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
if (!$Executable) { $Executable = Join-Path $repo 'Windows/VisualStudio/Build-vkQuake/x64/Debug/vkQuake.exe' }
$Executable = (Resolve-Path -LiteralPath $Executable).Path
$artifacts = Join-Path $repo ('build/rgss-smoke-' + [DateTime]::Now.ToString('yyyyMMdd-HHmmss-fff'))
$data = Join-Path $artifacts 'id1'
New-Item -ItemType Directory -Path $data -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $GameDirectory 'id1/pak0.pak') -Destination $data
$pak1 = Join-Path $GameDirectory 'id1/pak1.pak'
if (Test-Path -LiteralPath $pak1) { Copy-Item -LiteralPath $pak1 -Destination $data }

# Set the initial mode before the first render resources are created.
@('vid_fsaamode "4"', 'vid_fsaa "4"', 'r_oit "0"', 'r_ssao "0"') |
    Set-Content -LiteralPath (Join-Path $artifacts 'vkQuake.cfg') -Encoding ASCII
$cases = @(
    @{ Name = '2x-native-sorted'; Commands = @('vid_fsaamode 2', 'r_oit 0', 'r_ssao 0') },
    @{ Name = '4x-native-weighted-oit'; Commands = @('vid_fsaamode 3', 'r_oit 1') },
    @{ Name = '2x-native-moment-oit-ssao'; Commands = @('vid_fsaamode 2', 'r_oit 2', 'r_ssao 1') },
    @{ Name = '4x-native-ssao'; Commands = @('vid_fsaamode 3', 'r_ssao 3') },
    @{ Name = '4x-scaled'; Commands = @('r_width 400', 'r_height 300', 'r_ssao 2') },
    @{ Name = '2x-scaled-menu'; Commands = @('vid_fsaamode 2', 'menu_options') },
    @{ Name = '4x-resized-menu'; Commands = @('vid_fsaamode 3', 'vid_width 1024', 'vid_height 768', 'vid_restart') },
    @{ Name = '4x-no-tasks'; Commands = @('togglemenu', 'r_tasks 0', 'r_width -1', 'r_height -1', 'r_ssao 0', 'r_oit 0') },
    @{ Name = 'legacy-msaa'; Commands = @('r_tasks 1', 'vid_fsaamode 0', 'vid_fsaa 4') },
    @{ Name = 'legacy-supersample'; Commands = @('vid_fsaamode 1') },
    @{ Name = 'legacy-off'; Commands = @('vid_fsaamode 0', 'vid_fsaa 0') },
    @{ Name = '4x-after-off'; Commands = @('vid_fsaamode 3') },
    @{ Name = '8x-native-sorted'; Commands = @('vid_fsaamode 4') },
    @{ Name = '8x-native-weighted-oit'; Commands = @('r_oit 1') },
    @{ Name = '8x-native-moment-oit-ssao'; Commands = @('r_oit 2', 'r_ssao 3') },
    @{ Name = '8x-scaled-menu'; Commands = @('r_width 400', 'r_height 300', 'r_ssao 2', 'menu_options') },
    @{ Name = '8x-resized-menu'; Commands = @('vid_width 1280', 'vid_height 720', 'vid_restart') },
    @{ Name = '8x-no-tasks'; Commands = @('togglemenu', 'r_tasks 0', 'r_width -1', 'r_height -1', 'r_ssao 0', 'r_oit 0') },
    @{ Name = '4x-after-8x'; Commands = @('r_tasks 1', 'vid_fsaamode 3') },
    @{ Name = '2x-after-4x'; Commands = @('vid_fsaamode 2') }
)
$commands = [System.Collections.Generic.List[string]]::new()
$commands.AddRange([string[]]@('vid_unlock', 'vid_vsync 0', 'host_maxfps 60', 'con_notifytime 0', 'r_wateralpha 0.5', 'playdemo demo1'))
1..90 | ForEach-Object { $commands.Add('wait') }
foreach ($case in $cases) {
    $commands.Add('echo RGSS_CASE_' + $case.Name)
    $commands.AddRange([string[]]$case.Commands)
    1..30 | ForEach-Object { $commands.Add('wait') }
    $commands.Add('screenshot png')
    1..4 | ForEach-Object { $commands.Add('wait') }
}
$commands.Add('echo RGSS_TEST_DONE')
$commands.Add('quit')
$commands | Set-Content -LiteralPath (Join-Path $data 'rgss-smoke.cfg') -Encoding ASCII
$stdout = Join-Path $artifacts 'stdout.log'
$stderr = Join-Path $artifacts 'stderr.log'
$previousLayerPath = $env:VK_LAYER_PATH
try {
    if ($VulkanSdk) { $env:VK_LAYER_PATH = Join-Path $VulkanSdk 'Bin' }
    $run = Start-Process -FilePath $Executable -ArgumentList @(
        '-basedir', '.', '-window', '-width', '800', '-height', '600',
        '-nosound', '-validation', '2', '-condebug', '+exec', 'rgss-smoke.cfg'
    ) -WorkingDirectory $artifacts -WindowStyle Hidden -RedirectStandardOutput $stdout -RedirectStandardError $stderr -PassThru
    # Cache the handle before exit so Windows PowerShell can retrieve ExitCode.
    [void]$run.Handle
    Write-Output "Running RGSS validation (PID $($run.Id)); artifacts: $artifacts"
    $deadline = [DateTime]::UtcNow.AddSeconds(90)
    while (!$run.WaitForExit(1000)) {
        if ([DateTime]::UtcNow -gt $deadline) {
            $run.Kill()
            throw "Validation timed out; see $artifacts"
        }
    }
    $run.WaitForExit()
    if ($run.ExitCode -ne 0) { throw "Renderer exited with code $($run.ExitCode); see $artifacts" }
} finally {
    $env:VK_LAYER_PATH = $previousLayerPath
}
$log = (Get-Content -LiteralPath $stdout -Raw) + (Get-Content -LiteralPath $stderr -Raw)
if ($log -notmatch 'RGSS_TEST_DONE') { throw "Validation did not finish; see $artifacts" }
if ($log -notmatch 'VK_LAYER_KHRONOS_validation') { throw 'Validation layer was not enabled' }
foreach ($samples in @(2, 4, 8)) {
    if ($log -notmatch "3dfx ${samples}x RGSS: complete jittered frames") {
        throw "${samples}x RGSS must be supported and active for this test"
    }
}
$errors = Select-String -LiteralPath $stdout, $stderr -Pattern 'VUID-|hazard|Assertion|Validation Error|Sys_Error|Unknown command'
if ($errors) { $errors | Select-Object -First 10 | Write-Output; throw "Renderer/validation errors; see $artifacts" }
$screenshots = @(Get-ChildItem -LiteralPath $data -Filter '*.png' | Sort-Object Name)
if ($screenshots.Count -ne $cases.Count) { throw "Expected $($cases.Count) screenshots, found $($screenshots.Count)" }
Add-Type -AssemblyName System.Drawing
$results = for ($i = 0; $i -lt $cases.Count; ++$i) {
    $bitmap = [System.Drawing.Bitmap]::new($screenshots[$i].FullName)
    try {
        $expectedWidth = if ($i -ge 16) { 1280 } elseif ($i -ge 6) { 1024 } else { 800 }
        $expectedHeight = if ($i -ge 16) { 720 } elseif ($i -ge 6) { 768 } else { 600 }
        if ($bitmap.Width -ne $expectedWidth -or $bitmap.Height -ne $expectedHeight) { throw "Incorrect output dimensions in $($cases[$i].Name)" }
        $colors = [System.Collections.Generic.HashSet[int]]::new()
        for ($y = 0; $y -lt $bitmap.Height; $y += 8) {
            for ($x = 0; $x -lt $bitmap.Width; $x += 8) { [void]$colors.Add($bitmap.GetPixel($x, $y).ToArgb()) }
        }
        if ($colors.Count -lt 64) { throw "Blank or corrupt frame in $($cases[$i].Name)" }
        [pscustomobject]@{ Case = $cases[$i].Name; Screenshot = $screenshots[$i].Name; Colors = $colors.Count }
    } finally { $bitmap.Dispose() }
}
$results | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $artifacts 'results.json') -Encoding UTF8
$results | Format-Table -AutoSize
Write-Output "PASS: $($cases.Count) captures, mode switches, resize, and synchronization validation. Review images in $artifacts."
