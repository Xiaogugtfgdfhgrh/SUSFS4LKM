$ErrorActionPreference = "Stop"
$Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$Build = if ($env:SUSFS_BUILD_ROOT) { $env:SUSFS_BUILD_ROOT } else { "D:\CodexBuild\susfs4ksu-unified" }
$Clang = if ($env:SUSFS_CLANG) { $env:SUSFS_CLANG } else { "C:\Users\Admin（无密码）\AppData\Local\Android\Sdk\ndk\29.0.14206865\toolchains\llvm\prebuilt\windows-x86_64\bin\clang.exe" }
$Bash = if ($env:SUSFS_BASH) { $env:SUSFS_BASH } else { "C:\Program Files\Git\bin\bash.exe" }
$Python = if ($env:SUSFS_PYTHON) { $env:SUSFS_PYTHON } else { "C:\Users\Admin（无密码）\AppData\Local\Programs\Python\Python312\python.exe" }
$Objdump = if ($env:SUSFS_OBJDUMP) { $env:SUSFS_OBJDUMP } else { "C:\Users\Admin（无密码）\AppData\Local\Android\Sdk\ndk\29.0.14206865\toolchains\llvm\prebuilt\windows-x86_64\bin\llvm-objdump.exe" }
$KoDir = Join-Path $Build "ko"
$VariantList = Join-Path $Build "variants.txt"

New-Item -ItemType Directory -Force -Path $Build,(Join-Path $Build "dist") | Out-Null

& $Python -B (Join-Path $Root "tools\check-version.py") $Root
if ($LASTEXITCODE -ne 0) { throw "version metadata failed" }

& $Python -B (Join-Path $Root "tools\fetch-sources.py") $Root
if ($LASTEXITCODE -ne 0) { throw "sources fetch failed" }

& $Python -B (Join-Path $Root "tools\source_lock.py") $Root
if ($LASTEXITCODE -ne 0) { throw "source lock mismatch" }

$hashes = @{
    "sources\ksu_susfs\main.c" = "95100C0E3588A51BB26898558622F417476095BADE61CDDB4BF0DB28745ADE33"
    "sources\cjson\cJSON.c" = "298581A04A36C0165DA4B0AADE235C23088CB2FAA58651D720EA2F3706ED0B0D"
    "sources\cjson\cJSON.h" = "25B0145150D500498E4D209CEC69C18C42CF818BFFCC54690BE3B895A2A16DEE"
}
foreach ($rel in $hashes.Keys) {
    $actual = (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $Root $rel)).Hash
    if ($actual -ne $hashes[$rel]) { throw "hash mismatch: $rel" }
}

foreach ($file in Get-ChildItem -LiteralPath (Join-Path $Root "overlay\module") -Filter *.sh) {
    & $Bash -n $file.FullName
    if ($LASTEXITCODE -ne 0) { throw "shell syntax failed: $($file.Name)" }
}
foreach ($file in Get-ChildItem -LiteralPath (Join-Path $Root "tools") -Filter *.sh) {
    & $Bash -n $file.FullName
    if ($LASTEXITCODE -ne 0) { throw "shell syntax failed: $($file.Name)" }
}
foreach ($file in Get-ChildItem -LiteralPath (Join-Path $Root "overlay\webui") -Recurse -File -Filter *.js) {
    & node --check $file.FullName
    if ($LASTEXITCODE -ne 0) { throw "javascript syntax failed: $($file.Name)" }
}
& $Python -B (Join-Path $Root "tests\parse-sources.py") $Root
if ($LASTEXITCODE -ne 0) { throw "python parse failed" }

& $Clang --target=aarch64-linux-android21 -std=gnu11 -O2 -Wall -Wextra -Wno-unused-parameter -I (Join-Path $Root "sources\cjson") -fsyntax-only (Join-Path $Root "src\susfsctl\main.c")
if ($LASTEXITCODE -ne 0) { throw "susfsctl syntax failed" }

& $Clang --target=aarch64-linux-android21 -std=gnu11 -O2 -Wall -Wextra -Wno-unused-parameter -I (Join-Path $Root "sources\cjson") (Join-Path $Root "src\susfsctl\main.c") (Join-Path $Root "sources\cjson\cJSON.c") -o (Join-Path $Build "susfsctl")
if ($LASTEXITCODE -ne 0) { throw "susfsctl build failed" }

& $Clang --target=aarch64-linux-gnu -O2 -nostdlib -static-pie -fno-stack-protector -fno-builtin -fuse-ld=lld '-Wl,-e,_start' -o (Join-Path $Build "susfs_insmod") (Join-Path $Root "sources\lkm\tools\susfs_insmod.c")
if ($LASTEXITCODE -ne 0) { throw "susfs_insmod build failed" }

if (-not (Test-Path -LiteralPath $VariantList)) { throw "variant list missing: $VariantList" }
$variants = Get-Content -LiteralPath $VariantList | Where-Object { $_.Trim() }
& $Python -B (Join-Path $Root "tests\check-ko.py") $VariantList $KoDir
if ($LASTEXITCODE -ne 0) { throw "ko check failed" }
$koSourcesPath = Join-Path $KoDir "ko-sources.json"
$koSources = if (Test-Path -LiteralPath $koSourcesPath) { Get-Content -LiteralPath $koSourcesPath -Raw | ConvertFrom-Json } else { @{} }
foreach ($variant in $variants) {
    $realKo = Join-Path $KoDir "susfs_guard_lkm-$variant.ko"
    if (-not (Test-Path -LiteralPath $realKo)) { throw "real LKM missing: $realKo" }
    $source = $koSources.$variant.source
    if ($source -eq "current-source") {
        $disassembly = (& $Objdump -d $realKo) -join "`n"
        foreach ($symbol in @("kstat_wrap_nfstatat", "susfs_opath_openat")) {
            $pattern = "<$symbol>:\s*\n\s*[0-9a-f]+:\s+d503245f\s+bti\s+c"
            if ($disassembly -notmatch $pattern) {
                throw "missing BTI landing pad: $variant $symbol"
            }
        }
    }
}

$webroot = Join-Path $Build "webui\dist"
if (-not (Test-Path -LiteralPath $webroot)) { throw "webui dist missing" }
$zip = Join-Path $Build "dist\verify.zip"

& $Python -B (Join-Path $Root "tools\package.py") --root $Root --build-root $Build --ko-dir $KoDir --variant-list $VariantList --webroot $webroot --output $zip | Out-Null
if ($LASTEXITCODE -ne 0) { throw "package failed" }
$first = (Get-FileHash -Algorithm SHA256 -LiteralPath $zip).Hash
& $Python -B (Join-Path $Root "tools\package.py") --root $Root --build-root $Build --ko-dir $KoDir --variant-list $VariantList --webroot $webroot --output $zip | Out-Null
if ($LASTEXITCODE -ne 0) { throw "package rerun failed" }
$second = (Get-FileHash -Algorithm SHA256 -LiteralPath $zip).Hash
if ($first -ne $second) { throw "package is not deterministic" }

& $Python -B (Join-Path $Root "tests\check-zip.py") $zip $VariantList
if ($LASTEXITCODE -ne 0) { throw "zip layout failed" }

Write-Output "VERIFY_OK $first"
