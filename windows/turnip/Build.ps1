param(
    [string]$BuildDirectory = '',
    [string]$OutputDirectory = '',
    [string]$PythonExecutable = 'python',
    [ValidateRange(1,32)][int]$Jobs = 4
)

$ErrorActionPreference = 'Stop'
if (![OperatingSystem]::IsWindows() -or [Runtime.InteropServices.RuntimeInformation]::OSArchitecture -ne 'Arm64') {
    throw 'Build this port on Windows ARM64.'
}
$sourceRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
if (!$BuildDirectory) { $BuildDirectory = Join-Path $sourceRoot 'build-turnip-windows-arm64' }
if (!$OutputDirectory) { $OutputDirectory = Join-Path $sourceRoot 'dist-turnip-windows' }
$BuildDirectory = [IO.Path]::GetFullPath($BuildDirectory)
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
$toolRoot = Join-Path $sourceRoot '.turnip-tools'
New-Item -ItemType Directory -Path $toolRoot,$OutputDirectory -Force | Out-Null

$vswhere = @(
    "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe",
    "$env:ProgramFiles\Microsoft Visual Studio\Installer\vswhere.exe"
) | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if (!$vswhere) { throw 'Install Visual Studio with ARM64 C++ tools and the Windows SDK.' }
$vsRoot = & $vswhere -latest -products '*' -property installationPath
if (!$vsRoot) { throw 'Visual Studio was not found.' }
Import-Module (Join-Path $vsRoot 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $vsRoot -SkipAutomaticLocation -DevCmdArguments '-arch=arm64 -host_arch=arm64'
$llvmBin = Join-Path $vsRoot 'VC\Tools\Llvm\ARM64\bin'
if (!(Test-Path -LiteralPath (Join-Path $llvmBin 'clang-cl.exe'))) {
    $llvmBin = Split-Path (Get-Command clang-cl -ErrorAction Stop).Source
}

$venv = Join-Path $toolRoot 'venv'
$python = Join-Path $venv 'Scripts\python.exe'
if (!(Test-Path -LiteralPath $python)) {
    & $PythonExecutable -m venv $venv
    if ($LASTEXITCODE) { throw 'Could not create the build Python environment.' }
}
& $python -m pip install --disable-pip-version-check -r (Join-Path $PSScriptRoot 'requirements-build.txt')
if ($LASTEXITCODE) { throw 'Python build dependencies could not be installed.' }

$flexRoot = Join-Path $toolRoot 'winflexbison'
if (!(Test-Path -LiteralPath (Join-Path $flexRoot 'win_bison.exe'))) {
    $archive = Join-Path $toolRoot 'win_flex_bison-2.5.25.zip'
    Invoke-WebRequest 'https://github.com/lexxmark/winflexbison/releases/download/v2.5.25/win_flex_bison-2.5.25.zip' -OutFile $archive
    if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne '8D324B62BE33604B2C45AD1DD34AB93D722534448F55A16CA7292DE32B6AC135') {
        throw 'WinFlexBison archive checksum mismatch.'
    }
    Expand-Archive -LiteralPath $archive -DestinationPath $flexRoot -Force
}
$glslangRoot = Join-Path $toolRoot 'glslang'
$glslangValidator = Join-Path $glslangRoot 'bin\glslangValidator.exe'
if (!(Test-Path -LiteralPath $glslangValidator)) {
    $archive = Join-Path $toolRoot 'glslang-16.6.0-windows-x86_64-release.zip'
    if (!(Test-Path -LiteralPath $archive)) {
        Invoke-WebRequest 'https://github.com/KhronosGroup/glslang/releases/download/16.6.0/glslang-16.6.0-windows-x86_64-release.zip' -OutFile $archive
    }
    if ((Get-FileHash -LiteralPath $archive).Hash -ne '82BF434E69B9BB4829DE7E2B4BC2C5E7A7861E53D66CF75E5CC70F5F694A8D9B') {
        throw 'glslang archive checksum mismatch.'
    }
    Expand-Archive -LiteralPath $archive -DestinationPath $glslangRoot -Force
    Copy-Item -LiteralPath (Join-Path $glslangRoot 'bin\glslang.exe') -Destination $glslangValidator
}
& $glslangValidator --version
if ($LASTEXITCODE) { throw 'The build-time GLSL compiler could not run.' }
$env:PATH = "$venv\Scripts;$flexRoot;$glslangRoot\bin;$llvmBin;$env:PATH"
$gitUnixTools = Join-Path $env:ProgramFiles 'Git\usr\bin'
if (Test-Path -LiteralPath $gitUnixTools) { $env:PATH = "$env:PATH;$gitUnixTools" }
$env:CC = $env:CXX = 'clang-cl'
$env:PYTHONUTF8 = '1'
Get-Command ninja -ErrorAction Stop | Out-Null
$packageCache = Join-Path $sourceRoot 'subprojects\packagecache'
New-Item -ItemType Directory -Path $packageCache -Force | Out-Null
$zlibPackages = @(
    @{
        Name = 'zlib-1.3.1.tar.gz'
        Url = 'https://github.com/mesonbuild/wrapdb/releases/download/zlib_1.3.1-1/zlib-1.3.1.tar.gz'
        Hash = '9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23'
    },
    @{
        Name = 'zlib_1.3.1-1_patch.zip'
        Url = 'https://github.com/mesonbuild/wrapdb/releases/download/zlib_1.3.1-1/zlib_1.3.1-1_patch.zip'
        Hash = 'e79b98eb24a75392009cec6f99ca5cdca9881ff20bfa174e8b8926d5c7a47095'
    }
)
foreach ($package in $zlibPackages) {
    $packagePath = Join-Path $packageCache $package.Name
    if (!(Test-Path -LiteralPath $packagePath)) { Invoke-WebRequest $package.Url -OutFile $packagePath }
    if ((Get-FileHash -LiteralPath $packagePath).Hash -ne $package.Hash) { throw "Checksum mismatch: $($package.Name)" }
}
$directxRoot = Join-Path $sourceRoot 'subprojects\DirectX-Headers-1.0'
if (!(Test-Path -LiteralPath (Join-Path $directxRoot 'meson.build'))) {
    & git clone --depth 1 --branch v1.619.1 https://github.com/microsoft/DirectX-Headers.git $directxRoot
    if ($LASTEXITCODE) { throw 'DirectX-Headers could not be downloaded.' }
}
$directxRevision = (& git -C $directxRoot rev-parse HEAD).Trim()
if ($directxRevision -ne '9e393d6d8a3b30dcc6f2806ef604ec16a27b0d7e') { throw 'DirectX-Headers revision mismatch.' }
$nativeFile = Join-Path $toolRoot 'native.ini'
$pythonPath = $python.Replace('\','/')
@"
[binaries]
python = '$pythonPath'
python3 = '$pythonPath'
"@ | Set-Content -LiteralPath $nativeFile -Encoding utf8
$revision = (& git -C $sourceRoot rev-parse HEAD).Trim()
$configure = @('setup')
if (Test-Path -LiteralPath "$BuildDirectory\meson-private\coredata.dat") { $configure += '--reconfigure' }
$configure += @(
    $BuildDirectory,$sourceRoot,'--native-file',$nativeFile,'--buildtype=debugoptimized',
    '--default-library=static','--wrap-mode=nodownload',
    '-Dgallium-drivers=[]','-Dvulkan-drivers=freedreno','-Dfreedreno-kmds=gsl',
    "-Dtu-build-id=$revision",'-Dplatforms=windows','-Dglx=disabled','-Degl=disabled',
    '-Dgbm=disabled','-Dopengl=false','-Dgles1=disabled','-Dgles2=disabled',
    '-Dllvm=disabled','-Dshared-llvm=disabled','-Dvideo-codecs=[]','-Dbuild-tests=true',
    '-Dlibunwind=disabled','-Dlmsensors=disabled','-Dshader-cache=disabled',
    '-Dexpat=disabled','-Dzstd=disabled'
)
& $python -m mesonbuild.mesonmain @configure
if ($LASTEXITCODE) { throw 'Mesa configuration failed.' }
& ninja -C $BuildDirectory "-j$Jobs"
if ($LASTEXITCODE) { throw 'Turnip build failed.' }
& $python -m mesonbuild.mesonmain test -C $BuildDirectory --no-rebuild --print-errorlogs
if ($LASTEXITCODE) { throw 'Mesa CPU regressions failed.' }

$formatProbe = Join-Path $BuildDirectory 'turnip-format-check.exe'
$includePaths = @(
    "$sourceRoot\src", "$sourceRoot\include", "$sourceRoot\src\freedreno",
    "$BuildDirectory\src", "$BuildDirectory\src\freedreno\registers\adreno"
) | ForEach-Object { "/I$_" }
& clang-cl /nologo /std:c++20 /EHsc /MT /O2 /W3 /DHAVE_STRUCT_TIMESPEC @includePaths (Join-Path $PSScriptRoot 'format-check.cpp') "/Fe$formatProbe" "/Fo$BuildDirectory\turnip-format-check.obj"
if ($LASTEXITCODE) { throw 'Format regression could not be compiled.' }
& $formatProbe
if ($LASTEXITCODE) { throw 'Format storage regression failed.' }

$driver = Join-Path $BuildDirectory 'src\freedreno\vulkan\vulkan_freedreno.dll'
$driverBytes = [IO.File]::ReadAllBytes($driver)
$peOffset = [BitConverter]::ToInt32($driverBytes,60)
if ([BitConverter]::ToUInt16($driverBytes,$peOffset + 4) -ne 0xaa64) { throw 'The built ICD is not native ARM64.' }
Copy-Item -LiteralPath $driver -Destination $OutputDirectory -Force
$generatedManifest = Get-Content -LiteralPath (Join-Path $BuildDirectory 'src\freedreno\vulkan\freedreno_devenv_icd.aarch64.json') -Raw | ConvertFrom-Json
@{
    file_format_version = '1.0.0'
    ICD = @{ library_path = '.\vulkan_freedreno.dll'; api_version = $generatedManifest.ICD.api_version }
} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $OutputDirectory 'freedreno_icd.aarch64.json') -Encoding utf8
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'Run.ps1'),(Join-Path $PSScriptRoot 'README.zh-CN.md'),(Join-Path $PSScriptRoot 'NOTES.md') -Destination $OutputDirectory -Force
Copy-Item -LiteralPath (Join-Path $sourceRoot 'docs\license.rst') -Destination (Join-Path $OutputDirectory 'MESA-LICENSES.txt') -Force
Copy-Item -LiteralPath (Join-Path $directxRoot 'LICENSE') -Destination (Join-Path $OutputDirectory 'DIRECTX-HEADERS-LICENSE.txt') -Force
Copy-Item -LiteralPath (Join-Path $sourceRoot 'subprojects\zlib-1.3.1\LICENSE') -Destination (Join-Path $OutputDirectory 'ZLIB-LICENSE.txt') -Force
[ordered]@{
    source_revision = $revision
    built_at_utc = [DateTimeOffset]::UtcNow.ToString('o')
    architecture = 'ARM64'
    build_type = 'debugoptimized'
    gsl_abi_driver_tested = '31.0.170.0'
    cpu_tests = @('meson test','format-check')
    gpu_tests_run_by_build_script = $false
    dll_sha256 = (Get-FileHash -LiteralPath (Join-Path $OutputDirectory 'vulkan_freedreno.dll')).Hash
} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $OutputDirectory 'build-info.json') -Encoding utf8
Compress-Archive -Path "$OutputDirectory\*" -DestinationPath "$OutputDirectory.zip" -Force
Write-Host "Driver package: $OutputDirectory.zip"
