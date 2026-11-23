param([switch]$Active,[string]$CompilerBin='')
$ErrorActionPreference='Stop'
$cc = if ($CompilerBin) { Join-Path $CompilerBin 'i686-w64-mingw32-gcc.exe' } else { 'i686-w64-mingw32-gcc.exe' }
$cxx = if ($CompilerBin) { Join-Path $CompilerBin 'i686-w64-mingw32-g++.exe' } else { 'i686-w64-mingw32-g++.exe' }
$vendor=Join-Path $PSScriptRoot 'vendor/minhook-1.3.4'
$build=Join-Path $PSScriptRoot 'index-build'
New-Item -ItemType Directory -Force -Path $build | Out-Null
$objects=@()
foreach($source in @('buffer','hook','trampoline','hde/hde32')) {
    $object=Join-Path $build (($source -replace '/','-')+'.o')
    & $cc -O2 -I "$vendor/include" -c "$vendor/src/$source.c" -o $object
    if($LASTEXITCODE){throw "Compiler failed: $source"};$objects+=$object
}
$flags=@('-std=c++17','-O2','-Wall','-Wextra','-Werror','-Wno-cast-function-type','-static','-I',"$vendor/include")
$modeFlags=@(if($Active){'-DLOADING_INDEX_ACTIVE=1'})
$testName=if($Active){'LoadingIndexActiveTest.exe'}else{'LoadingIndexTest.exe'}
$binaryName=if($Active){'TS2-LoadingIndex.asi'}else{'TS2-LoadingIndexValidation.asi'}
& $cxx @flags @modeFlags "$PSScriptRoot/LoadingIndexTest.cpp" @objects -o "$build/$testName"
if($LASTEXITCODE){throw 'Index test build failed'}
& "$build/$testName" "$PSScriptRoot/native-membership-snapshot.bin"
if($LASTEXITCODE){throw 'Index test failed'}
& $cxx @flags @modeFlags -shared "$PSScriptRoot/LoadingIndexValidation.cpp" @objects -o "$build/$binaryName"
if($LASTEXITCODE){throw 'Validation ASI build failed'}
Get-FileHash "$build/$binaryName" -Algorithm SHA256

