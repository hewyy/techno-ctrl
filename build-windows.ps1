$ErrorActionPreference = "Stop"

$ProjectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$BuildDirectory = Join-Path $ProjectRoot "build"

cmake -S $ProjectRoot -B $BuildDirectory -G "Visual Studio 17 2022" -A x64
cmake --build $BuildDirectory --config Release
ctest --test-dir $BuildDirectory -C Release --output-on-failure

$PluginPath = Join-Path $BuildDirectory "LivePatternSequencer_artefacts\Release\VST3\Live Pattern Sequencer MVP.vst3"

Write-Host ""
Write-Host "Build complete."
Write-Host "Plugin: $PluginPath"
Write-Host ""
Write-Host "Copy the .vst3 bundle to:"
Write-Host "C:\Program Files\Common Files\VST3\"

