<#
.SYNOPSIS
    Moku 的按板构建入口；共用 Python 构建器生成独立源码视图和 USB 包。
    Moku's board entry; use the common Python builder for isolated source views and USB bundles.
.DESCRIPTION
    中文：默认使用普通构建（S31 原生 48 相阅读翻页与 C 像素算子）。用户 2026-11-12 回退后，
    S31 的 PIE/文字波纹/GL37 面板实验不再默认开启，需要显式加 -S31ReaderExperiments；
    -Plain 保留为兼容参数，效果与默认相同。该组实验只影响 S31，Pico 不受影响。
    English: Plain builds are the default (native S31 reader turn and C pixel kernels). After the
    2026-11-12 rollback the S31 PIE/text-ripple/reader-panel37 experiments are no longer enabled
    by default; pass -S31ReaderExperiments to opt in. -Plain is kept for compatibility and equals
    the default. These switches only affect S31; Pico is unchanged.
    -Board keeps the historical metalio_s31 alias.
.EXAMPLE
    .\tools\build_board.ps1 -Board metalio_s31
.EXAMPLE
    .\tools\build_board.ps1 -Board metalio47 -S31ReaderExperiments
#>
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet("pico", "metalio47", "metalio_s31", "all")]
    [string]$Board,
    [string]$Output = "build/boards",
    [string]$Python = "python",
    [switch]$Plain,
    [switch]$S31ReaderExperiments
)
$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
if (-not $env:IDF_PATH) { throw "先载入对应板型的 ESP-IDF / Load this board's ESP-IDF first." }
$extra = @()
if ($S31ReaderExperiments -and $Board -in @("metalio47", "metalio_s31", "all")) {
    $extra = @("--s31-pie", "--s31-text-ripple", "--s31-reader-panel37")
}
Push-Location $repo
try {
    $pythonPath = (Get-Command $Python -ErrorAction Stop).Source
    & $pythonPath tools/build_firmware.py --board $Board --output $Output --python $pythonPath @extra
    if ($LASTEXITCODE -ne 0) { throw "Board build failed ($LASTEXITCODE)" }
} finally { Pop-Location }
