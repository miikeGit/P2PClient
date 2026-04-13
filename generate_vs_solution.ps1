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

$cmakeArgs = @(
    "-S", ".",
    "-B", "__BUILD_VS${vsYear}_x64",
    "-G", "Visual Studio $vsMajor $vsYear",
    "-A", "x64",
    "-DCMAKE_PREFIX_PATH=$qtDir",
    "-DCMAKE_POLICY_VERSION_MINIMUM=3.5"
)

cmake @cmakeArgs

if ($LASTEXITCODE -ne 0) { throw "CMake error!" }