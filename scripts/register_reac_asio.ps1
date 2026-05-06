param(
    [string]$Device = "Realtek",
    [string]$Output = "Speakers",
    [switch]$Machine
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$dll = Join-Path $repoRoot "reac_asio.dll"
$clsid = "{8E2B2FD8-7D31-4A19-8C73-0D6D8285C63B}"
$driverName = "REAC 40ch ASIO"

if (!(Test-Path -LiteralPath $dll)) {
    throw "Could not find $dll. Build reac_asio.dll first."
}

[Environment]::SetEnvironmentVariable("REAC_ASIO_DEVICE", $Device, "User")
[Environment]::SetEnvironmentVariable("REAC_ASIO_OUTPUT", $Output, "User")

$settingsKey = "HKCU:\Software\REAC Decoder"
New-Item -Path $settingsKey -Force | Out-Null
New-ItemProperty -Path $settingsKey -Name CaptureDevice -Value $Device -PropertyType String -Force | Out-Null
New-ItemProperty -Path $settingsKey -Name OutputDevice -Value $Output -PropertyType String -Force | Out-Null

$runtimeDlls = @(
    "C:\msys64\ucrt64\bin\libstdc++-6.dll",
    "C:\msys64\ucrt64\bin\libgcc_s_seh-1.dll",
    "C:\msys64\ucrt64\bin\libwinpthread-1.dll"
)

foreach ($runtimeDll in $runtimeDlls) {
    if (Test-Path -LiteralPath $runtimeDll) {
        Copy-Item -LiteralPath $runtimeDll -Destination $repoRoot -Force
    }
}

$regsvr = Join-Path $env:SystemRoot "System32\regsvr32.exe"
$process = Start-Process -FilePath $regsvr -ArgumentList "/s", "`"$dll`"" -Wait -PassThru
if ($process.ExitCode -ne 0) {
    throw "regsvr32 failed with exit code $($process.ExitCode)"
}

$asioKey = "HKCU:\Software\ASIO\REAC 40ch ASIO"
if (!(Test-Path -LiteralPath $asioKey)) {
    throw "Registration finished, but $asioKey was not created."
}

if ($Machine) {
    $principal = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
    $isAdmin = $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
    if (!$isAdmin) {
        throw "Machine-wide ASIO registration requires an elevated PowerShell."
    }

    $machineAsioKey = "HKLM:\SOFTWARE\ASIO\$driverName"
    New-Item -Path $machineAsioKey -Force | Out-Null
    New-ItemProperty -Path $machineAsioKey -Name CLSID -Value $clsid -PropertyType String -Force | Out-Null
    New-ItemProperty -Path $machineAsioKey -Name Description -Value $driverName -PropertyType String -Force | Out-Null

    $machineClsidKey = "HKLM:\SOFTWARE\Classes\CLSID\$clsid"
    $machineInprocKey = "$machineClsidKey\InprocServer32"
    New-Item -Path $machineInprocKey -Force | Out-Null
    Set-ItemProperty -Path $machineClsidKey -Name "(default)" -Value $driverName
    Set-ItemProperty -Path $machineInprocKey -Name "(default)" -Value $dll
    New-ItemProperty -Path $machineInprocKey -Name ThreadingModel -Value Both -PropertyType String -Force | Out-Null
}

Write-Host "Registered REAC 40ch ASIO"
Write-Host "REAC_ASIO_DEVICE=$Device"
Write-Host "REAC_ASIO_OUTPUT=$Output"
if ($Machine) {
    Write-Host "Machine-wide ASIO keys were written."
}
Write-Host "Restart Reaper, then select ASIO driver: REAC 40ch ASIO"
