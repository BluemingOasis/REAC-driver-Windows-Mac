$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$dll = Join-Path $repoRoot "reac_asio.dll"

if (!(Test-Path -LiteralPath $dll)) {
    throw "Could not find $dll."
}

$regsvr = Join-Path $env:SystemRoot "System32\regsvr32.exe"
Start-Process -FilePath $regsvr -ArgumentList "/s", "/u", "`"$dll`"" -Wait

$principal = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
if ($principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Remove-Item -LiteralPath "HKLM:\SOFTWARE\ASIO\REAC 40ch ASIO" -Recurse -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath "HKLM:\SOFTWARE\Classes\CLSID\{8E2B2FD8-7D31-4A19-8C73-0D6D8285C63B}" -Recurse -Force -ErrorAction SilentlyContinue
}

Write-Host "Unregistered REAC 40ch ASIO"
