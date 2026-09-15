param(
    [string]$Sdk = "$env:LOCALAPPDATA\Android\Sdk",
    [string]$Jdk = "$env:ProgramFiles\Android\Android Studio\jbr",
    [string]$Source = "$PSScriptRoot\..\..\third_party\OpenXR-SDK-Source"
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path "$PSScriptRoot\..\..").Path
$Source = (Resolve-Path $Source).Path
$build = "$root\build-hello-xr-windows"
$bt = "$Sdk\build-tools\36.1.0"
function Run([string]$Exe, [string[]]$Arguments) {
    & $Exe @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Exe failed ($LASTEXITCODE)" }
}
# Apply only this branch's sample helper patch; preserve other source edits.
$ErrorActionPreference = 'Continue'
& git -C $Source apply --reverse --check "$PSScriptRoot\emulator-pbuffer.patch" 2>$null
$ErrorActionPreference = 'Stop'
if ($LASTEXITCODE -ne 0) {
    Run 'git' @('-C', $Source, 'apply', '--check', "$PSScriptRoot\emulator-pbuffer.patch")
    Run 'git' @('-C', $Source, 'apply', "$PSScriptRoot\emulator-pbuffer.patch")
}
Run 'cmake' @('-S', $Source, '-B', $build, '-G', 'Ninja', '-Wno-deprecated', "-DCMAKE_MAKE_PROGRAM=$Sdk/cmake/3.22.1/bin/ninja.exe", "-DCMAKE_TOOLCHAIN_FILE=$Sdk/ndk/27.3.13750724/build/cmake/android.toolchain.cmake", '-DANDROID_ABI=x86_64', '-DANDROID_PLATFORM=android-29', '-DCMAKE_BUILD_TYPE=Release', '-DBUILD_API_LAYERS=OFF', '-DBUILD_TESTS=ON', '-DBUILD_LOADER=ON', '-DBUILD_CONFORMANCE_TESTS=OFF', '-DBUILD_ALL_EXTENSIONS=ON', '-DHELLOXR_DEFAULT_GRAPHICS_PLUGIN=OpenGLES')
Run 'cmake' @('--build', $build, '--target', 'hello_xr', '-j', '8')
New-Item -ItemType Directory -Force "$build\package\lib\x86_64" | Out-Null
Copy-Item "$build\src\tests\hello_xr\libhello_xr.so" "$build\package\lib\x86_64" -Force
Copy-Item "$build\src\loader\libopenxr_loader.so" "$build\package\lib\x86_64" -Force
Run "$bt\aapt2.exe" @('link', '-I', "$Sdk\platforms\android-34\android.jar", '--manifest', "$PSScriptRoot\emulator\AndroidManifest.xml", '-o', "$build\sample.apk")
Run "$Jdk\bin\jar.exe" @('uf', "$build\sample.apk", '-C', "$build\package", 'lib')
Run "$bt\zipalign.exe" @('-f', '-p', '4', "$build\sample.apk", "$build\aligned.apk")
$oldJavaHome = $env:JAVA_HOME
try {
    $env:JAVA_HOME = $Jdk
    Run "$bt\apksigner.bat" @('sign', '--ks', "$root\build-android-runtime-windows-x86_64\debug.keystore", '--ks-pass', 'pass:android', '--out', "$build\hello-xr-emulator.apk", "$build\aligned.apk")
    Run "$bt\apksigner.bat" @('verify', "$build\hello-xr-emulator.apk")
} finally { $env:JAVA_HOME = $oldJavaHome }
Write-Host "Built $build\hello-xr-emulator.apk"
