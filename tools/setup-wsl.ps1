$ErrorActionPreference = "Stop"
$Wsl = "D:\WSL\WSL\wsl.exe"
$Distro = "Ubuntu"
$Location = "D:\WSL\Ubuntu-susfs"

if (-not (Test-Path -LiteralPath $Wsl)) {
    throw "WSL runtime missing: $Wsl"
}

$installed = & $Wsl --list --quiet 2>$null
if ($LASTEXITCODE -ne 0) {
    throw "WSL optional component is not ready; reboot Windows and rerun"
}
if ($installed -notcontains $Distro) {
    & $Wsl --install -d Ubuntu --location $Location --no-launch --web-download
    if ($LASTEXITCODE -ne 0) { throw "Ubuntu install failed" }
}

& $Wsl -d $Distro -u root sh -c "apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y docker.io && service docker start"
if ($LASTEXITCODE -ne 0) { throw "docker setup failed" }

& $Wsl -d $Distro -u root docker version
if ($LASTEXITCODE -ne 0) { throw "docker is not ready" }

Write-Output "WSL_BUILD_READY $Distro"
