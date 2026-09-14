$ErrorActionPreference = "Stop"

$repo = "Qwen/Qwen3-VL-8B-Instruct-GGUF"
$model = "Qwen3VL-8B-Instruct-Q4_K_M.gguf"
$mmproj = "mmproj-Qwen3VL-8B-Instruct-Q8_0.gguf"
$destination = "models/vision"

if (-not (Get-Command hf -ErrorAction SilentlyContinue)) {
    throw @"
The Hugging Face 'hf' command was not found.

Install/update huggingface_hub first, for example:
    py -m pip install -U huggingface_hub

Then rerun this script from Rose's repository root.
"@
}

New-Item -ItemType Directory -Force -Path $destination | Out-Null

Write-Host "Downloading Rose semantic-vision models into $destination ..."

hf download $repo $model $mmproj --local-dir $destination

$modelPath = Join-Path $destination $model
$mmprojPath = Join-Path $destination $mmproj

if (-not (Test-Path $modelPath)) {
    throw "Vision language model was not downloaded: $modelPath"
}

if (-not (Test-Path $mmprojPath)) {
    throw "Vision projection model was not downloaded: $mmprojPath"
}

Write-Host ""
Write-Host "Vision model files are present:"
Get-Item $modelPath, $mmprojPath | Select-Object FullName, Length
