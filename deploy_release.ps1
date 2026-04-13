$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vsInfo = & $vswhere -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -format json | ConvertFrom-Json

if (!$vsInfo) { throw "Visual Studio not found!" }

$vsMajor = $vsInfo.installationVersion.Split('.')[0]

if ($vsInfo.displayName -match '(\d{4})') {
    $vsYear = $matches[1]
} else {
    $vsYear = $vsInfo.catalog.productLineVersion
}

$qtDir = Get-ChildItem -Path "C:\Qt" -Filter "msvc*_64" -Recurse -Depth 2 -ErrorAction SilentlyContinue | Select-Object -First 1 -ExpandProperty FullName
if (!$qtDir) { throw "Qt msvc64 not found!" }

$buildDir = "__BUILD_VS${vsYear}_x64"
$deployDir = "Deploy_Release"
$zipPath = "P2PClient_Release.zip"

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
    "--config", "Release",
    "--target", "P2PClient",
    "--parallel"
)
 
cmake @cmakeBuildArgs
if ($LASTEXITCODE -ne 0) { throw "Build failed!" }

if (Test-Path $deployDir) { Remove-Item -Path $deployDir -Recurse -Force }
New-Item -ItemType Directory -Path $deployDir | Out-Null

$exeSource = "$buildDir\Release\P2PClient.exe"
Copy-Item -Path $exeSource -Destination $deployDir

$windeployqt = "$qtDir\bin\windeployqt.exe"
if (Test-Path $windeployqt) {
    & $windeployqt --release "$deployDir\P2PClient.exe"
} else {
    Write-Warning "windeployqt.exe not found!"
}

$qmqttDll = "$buildDir\_deps\qmqtt-build\Release\qmqtt.dll"
if (Test-Path $qmqttDll) {
    Copy-Item -Path $qmqttDll -Destination $deployDir -Force
} else {
    Write-Warning "qmqtt.dll not found!"
}

$datachannelDll = "$buildDir\_deps\datachannel-build\Release\datachannel.dll"
if (Test-Path $datachannelDll) {
    Copy-Item -Path $datachannelDll -Destination $deployDir -Force
} else {
    Write-Warning "datachannel.dll not found!"
}

if (Test-Path $zipPath) { Remove-Item -Path $zipPath -Force }
Compress-Archive -Path "$deployDir\*" -DestinationPath $zipPath -Force

Write-Host "Done!"