# Build, then load gdaccess.dll into Grim Dawn.
#   .\tools\inject.ps1 -Launch    build, then start the game UNFOCUSED with the DLL injected before it initializes,
#                                 speech + game audio muted (pass -Speak to hear it), then wait for the dev server
#   .\tools\inject.ps1            build, then (re)inject into the running game: ejects the old DLL first so the
#                                 build can overwrite it -- this is the hot-reload loop
#   .\tools\inject.ps1 -Eject     unload only
#   -NoBuild                      skip the build step
param([switch]$Launch, [switch]$Eject, [switch]$NoBuild, [switch]$Speak, [int]$Port = 8791,
      [string]$GameExe = "C:\Program Files (x86)\Steam\steamapps\common\Grim Dawn\x64\Grim Dawn.exe")
$root = Split-Path $PSScriptRoot -Parent
$dll = "$root\build\ninja\gdaccess.dll"
$inj = "$root\build\ninja\gdinject.exe"
$running = [bool](Get-Process -Name "Grim Dawn" -ErrorAction SilentlyContinue)
if ($Eject) { & $inj --eject $dll; exit $LASTEXITCODE }
if ($running -and -not $Launch -and (Test-Path $inj)) { & $inj --eject $dll | Out-Null; Start-Sleep -Milliseconds 500 }
if (-not $NoBuild) {
  & cmd /c "$root\tools\build.cmd"
  if ($LASTEXITCODE -ne 0) { exit 1 }
}
if ($Launch) {
  if ($running) { Write-Host "Grim Dawn is already running; close it first (or use plain inject)."; exit 1 }
  # The engine stops ticking while unfocused when inactiveUpdateRate is 0; the dev loop needs it ticking.
  $opt = "$env:USERPROFILE\Documents\My Games\Grim Dawn\Settings\options.txt"
  if (Test-Path $opt) {
    $txt = Get-Content $opt -Raw
    if ($txt -match 'inactiveUpdateRate\s*=\s*0\b') {
      $txt = $txt -replace 'inactiveUpdateRate\s*=\s*0\b', 'inactiveUpdateRate        = 30'
      Set-Content -Path $opt -Value $txt -Encoding ascii -NoNewline
      Write-Host "options.txt: inactiveUpdateRate 0 -> 30 (keeps the game ticking while unfocused)"
    }
  }
  if ($Speak) { Remove-Item Env:\GDACCESS_MUTE -ErrorAction SilentlyContinue } else { $env:GDACCESS_MUTE = '1' }
  $env:GDACCESS_PORT = "$Port"
  $env:GDACCESS_NOFOCUS = '1'
  & $inj --launch $GameExe $dll
  if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
  Write-Host "waiting for the dev server on 127.0.0.1:$Port ..."
  $deadline = (Get-Date).AddSeconds(45)
  while ((Get-Date) -lt $deadline) {
    try { $r = Invoke-WebRequest -Uri "http://127.0.0.1:$Port/health" -TimeoutSec 2 -UseBasicParsing; Write-Host $r.Content; exit 0 } catch {}
    $gp = Get-Process -Name "Grim Dawn" -ErrorAction SilentlyContinue
    if (-not $gp) { Write-Host "game exited before the dev server came up; see %LOCALAPPDATA%\gdaccess\gdaccess.log"; exit 1 }
    Start-Sleep -Seconds 1
  }
  # A crashed game sits in its crash-reporter dialog forever; do not leave those around.
  Write-Host "timed out waiting for /health; killing the instance (check gdaccess.log / tools/stacks.py next time)"
  Get-Process -Name "Grim Dawn" -ErrorAction SilentlyContinue | Stop-Process -Force
  exit 1
}
& $inj $dll
exit $LASTEXITCODE
