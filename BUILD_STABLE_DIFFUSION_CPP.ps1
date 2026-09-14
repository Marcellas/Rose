$ErrorActionPreference = "Stop"

$Repo = "external/stable-diffusion.cpp"
$Build = "build-sd"
$PinnedCommit = "42d6c0ab92fe6595776b28e3f7c8925db79b31f5"

if (-not (Test-Path $Repo)) {
    git clone --recursive https://github.com/leejet/stable-diffusion.cpp $Repo
}

git -C $Repo fetch origin
git -C $Repo checkout $PinnedCommit
git -C $Repo submodule update --init --recursive

cmake -S $Repo -B $Build `
    -DSD_CUDA=ON `
    -DSD_WEBP=OFF `
    -DSD_WEBM=OFF

cmake --build $Build --config Release -j 8

Write-Host ""
Write-Host "stable-diffusion.cpp build complete."
Write-Host "Expected CLI: $Build/bin/Release/sd-cli.exe"
