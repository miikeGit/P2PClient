$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vsInfo = & $vswhere -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -format json | ConvertFrom-Json

if (!$vsInfo) { throw "Visual Studio with C++ support not found!" }

$vsMajor = $vsInfo.installationVersion.Split('.')[0]

if ($vsInfo.displayName -match '(\d{4})') {
    $vsYear = $matches[1]
} else {
    $vsYear = $vsInfo.catalog.productLineVersion
}

$qtDir = Get-ChildItem -Path "C:\Qt" -Filter "msvc*_64" -Recurse -Depth 2 -ErrorAction SilentlyContinue | Select-Object -First 1 -ExpandProperty FullName
if (!$qtDir) { throw "Qt msvc64 not found in C:\Qt!" }

$buildDir = "__BUILD_VS${vsYear}_x64"

$cmakeArgs = @(
    "-S", ".",
    "-B", $buildDir,
    "-G", "Visual Studio $vsMajor $vsYear",
    "-A", "x64",
    "-DCMAKE_PREFIX_PATH=$qtDir",
    "-DCMAKE_POLICY_VERSION_MINIMUM=3.5"
)

cmake @cmakeArgs
if ($LASTEXITCODE -ne 0) { throw "CMake error!" }

$cmakeBuildArgs = @(
    "--build", $buildDir,
    "--config", "Debug",
    "--target", "P2PClient",
    "--parallel"
)
 
cmake @cmakeBuildArgs
if ($LASTEXITCODE -ne 0) { throw "Build failed!" }

$exeDir = "$buildDir\Debug"
$exePath = "$exeDir\P2PClient.exe"
$windeployqt = "$qtDir\bin\windeployqt.exe"

if (Test-Path $windeployqt) {
    & $windeployqt $exePath
} else {
    Write-Warning "windeployqt.exe not found!"
}

$qmqttDll = "$buildDir\_deps\qmqtt-build\Debug\qmqtt.dll"
if (Test-Path $qmqttDll) {
    Copy-Item -Path $qmqttDll -Destination $exeDir -Force
} else {
    Write-Warning "qmqtt.dll not found!"
}

$datachannelDll = "$buildDir\_deps\datachannel-build\Debug\datachannel.dll"
if (Test-Path $datachannelDll) {
    Copy-Item -Path $datachannelDll -Destination $exeDir -Force
} else {
    Write-Warning "datachannel.dll not found!"
}

Write-Host "Done!"