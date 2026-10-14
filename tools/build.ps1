param(
    [switch]$SkipLkm,
    [switch]$SkipWeb,
    [switch]$SkipHelper
)

$ErrorActionPreference = "Stop"
$Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$BuildRoot = if ($env:SUSFS_BUILD_ROOT) { $env:SUSFS_BUILD_ROOT } else { "D:\CodexBuild\susfs4ksu-unified" }
$Dist = Join-Path $BuildRoot "dist"
$WebStage = Join-Path $BuildRoot "webui"
$KoDir = Join-Path $BuildRoot "ko"
$VariantList = Join-Path $Root "tools\variants.txt"
$Wsl = if ($env:SUSFS_WSL) { $env:SUSFS_WSL } else { "D:\WSL\WSL\wsl.exe" }
$WslRoot = if ($env:SUSFS_WSL_ROOT) { $env:SUSFS_WSL_ROOT } else { "/mnt/c/Users/Admin（无密码）/Documents/ChatGPT/9989/SUSFS4LKM" }
$WslBuild = if ($env:SUSFS_WSL_BUILD) { $env:SUSFS_WSL_BUILD } else { "/mnt/d/CodexBuild/susfs4ksu-unified" }
$DdkDigests = @{
    "android12-5.10" = "sha256:272ada1e61c525116032246df2a790d1ccec1804b7a9e0b09203c5e650dcb78e"
    "android13-5.10" = "sha256:e5ce038ba38408ad26aae1f2622cfb44dfbf6634b025bf197aa08beac0794f5b"
    "android13-5.15" = "sha256:9e68cd9fcab748584755d31ba88ef134efc938dbde17f258c2652315f6cd81cd"
    "android14-5.15" = "sha256:bbf697a8fe995704268eef710921c9d2ea4f146954690d7ea97887f3a4014a6a"
    "android14-6.1" = "sha256:1dd6ac340b627a90a4031d6d0df6b129d8cc949b139fb56032c898f023d3d5d3"
    "android15-6.6" = "sha256:51159bc935c75a282fc65bc87e02f16bd098d1cdf55dc558c4fdd0fcf7a132e2"
    "android16-6.12" = "sha256:fd3ec5cc71d7f2716212e9df8d5674a9afe68fb769282fcac87b83f207f949ee"
    "android17-6.18" = "sha256:fb8b66200bf402cf65a751060fc1aa29524b9fa8840feef16c7b196a4f3f0e6e"
}
$Clang = if ($env:SUSFS_CLANG) { $env:SUSFS_CLANG } else { "C:\Users\Admin（无密码）\AppData\Local\Android\Sdk\ndk\29.0.14206865\toolchains\llvm\prebuilt\windows-x86_64\bin\clang.exe" }
$Python = if ($env:SUSFS_PYTHON) { $env:SUSFS_PYTHON } else { "C:\Users\Admin（无密码）\AppData\Local\Programs\Python\Python312\python.exe" }
$SourceBuilt = @(Get-Content -LiteralPath (Join-Path $Root "tools\variants.txt") | Where-Object { $_.Trim() })

New-Item -ItemType Directory -Force -Path $BuildRoot,$Dist | Out-Null

function Remove-SafeTree([string]$Path) {
    $resolved = [IO.Path]::GetFullPath($Path)
    $root = [IO.Path]::GetFullPath($BuildRoot)
    if (-not $resolved.StartsWith($root + [IO.Path]::DirectorySeparatorChar)) {
        throw "refusing to remove outside build root: $resolved"
    }
    if (Test-Path -LiteralPath $resolved) {
        Remove-Item -LiteralPath $resolved -Recurse -Force
    }
}

function Assert-Hash([string]$Path, [string]$Expected) {
    $actual = (Get-FileHash -Algorithm SHA256 -LiteralPath $Path).Hash
    if ($actual -ne $Expected) {
        throw "hash mismatch: $Path"
    }
}

function Read-Prop([string]$Path) {
    $values = @{}
    foreach ($line in [IO.File]::ReadAllLines($Path)) {
        $parts = $line.Split("=", 2)
        if ($parts.Count -eq 2) {
            $values[$parts[0].Trim()] = $parts[1].Trim()
        }
    }
    return $values
}

& $Python -B (Join-Path $Root "tools\fetch-sources.py") $Root
if ($LASTEXITCODE -ne 0) { throw "sources fetch failed" }
& $Python -B (Join-Path $Root "tools\source_lock.py") $Root
if ($LASTEXITCODE -ne 0) { throw "source lock mismatch" }

Assert-Hash (Join-Path $Root "sources\ksu_susfs\main.c") "95100C0E3588A51BB26898558622F417476095BADE61CDDB4BF0DB28745ADE33"
Assert-Hash (Join-Path $Root "sources\cjson\cJSON.c") "298581A04A36C0165DA4B0AADE235C23088CB2FAA58651D720EA2F3706ED0B0D"
Assert-Hash (Join-Path $Root "sources\cjson\cJSON.h") "25B0145150D500498E4D209CEC69C18C42CF818BFFCC54690BE3B895A2A16DEE"

if (-not $SkipHelper) {
    & $Clang --target=aarch64-linux-android21 -std=gnu11 -O2 -Wall -Wextra -Wno-unused-parameter -I (Join-Path $Root "sources\cjson") (Join-Path $Root "src\susfsctl\main.c") (Join-Path $Root "sources\cjson\cJSON.c") -o (Join-Path $BuildRoot "susfsctl")
    if ($LASTEXITCODE -ne 0) { throw "susfsctl build failed" }
    & $Clang --target=aarch64-linux-gnu -O2 -nostdlib -static-pie -fno-stack-protector -fno-builtin -fuse-ld=lld '-Wl,-e,_start' -o (Join-Path $BuildRoot "susfs_insmod") (Join-Path $Root "sources\lkm\tools\susfs_insmod.c")
    if ($LASTEXITCODE -ne 0) { throw "susfs_insmod build failed" }
}

if (-not $SkipWeb) {
    Remove-SafeTree $WebStage
    Copy-Item -LiteralPath (Join-Path $Root "sources\webui") -Destination $WebStage -Recurse
    Copy-Item -Path (Join-Path $Root "overlay\webui\*") -Destination $WebStage -Recurse -Force
    Push-Location $WebStage
    try {
        npm ci --cache (Join-Path $BuildRoot "npm-cache")
        if ($LASTEXITCODE -ne 0) { throw "npm ci failed" }
        npm run build
        if ($LASTEXITCODE -ne 0) { throw "webui build failed" }
    } finally {
        Pop-Location
    }
}

if (-not $SkipLkm) {
    foreach ($variant in $SourceBuilt) {
        $digest = $DdkDigests[$variant]
        & $Wsl -d Ubuntu sh -c "cd '$WslRoot' && SUSFS_BUILD_ROOT='$WslBuild' SUSFS_DDK_DIGEST='$digest' sh tools/build-lkm.sh '$WslRoot' '$WslBuild' '$variant'"
        if ($LASTEXITCODE -ne 0) { throw "lkm build failed: $variant" }
    }
    $koArgs = @()
    foreach ($variant in $SourceBuilt) {
        $koArgs += @("--skip", $variant, "--record", $variant)
    }
    & $Python -B (Join-Path $Root "tools\fetch-ko.py") --out $KoDir --variants $VariantList @koArgs
    if ($LASTEXITCODE -ne 0) { throw "pinned ko fetch failed" }
}

$props = Read-Prop (Join-Path $Root "overlay\module\module.prop")
$Zip = Join-Path $Dist "SUSFS4LKM-$($props['version'])-arm64.zip"
& $Python -B (Join-Path $Root "tools\package.py") --root $Root --build-root $BuildRoot --ko-dir $KoDir --variant-list $VariantList --webroot (Join-Path $WebStage "dist") --output $Zip
if ($LASTEXITCODE -ne 0) { throw "package failed" }

$Release = Join-Path $BuildRoot "release"
Remove-SafeTree $Release
New-Item -ItemType Directory -Force -Path $Release | Out-Null
Copy-Item -LiteralPath $Zip -Destination (Join-Path $Release "SUSFS4LKM-arm64.zip") -Force
Copy-Item -LiteralPath ([IO.Path]::ChangeExtension($Zip, ".provenance.json")) -Destination (Join-Path $Release "SUSFS4LKM-arm64.provenance.json") -Force
Copy-Item -LiteralPath (Join-Path $Root "tools\ROLLBACK.sh") -Destination (Join-Path $Release "ROLLBACK.sh") -Force
$sumNames = @("SUSFS4LKM-arm64.zip","SUSFS4LKM-arm64.provenance.json","ROLLBACK.sh")
$sumLines = foreach ($name in $sumNames) {
    $hash = (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $Release $name)).Hash
    "$hash  $name"
}
[IO.File]::WriteAllText((Join-Path $Release "SHA256SUMS"), (($sumLines -join "`n") + "`n"), (New-Object Text.UTF8Encoding($false)))
Write-Output "BUILD_OK $Zip"
