$ErrorActionPreference = "Stop"

New-Item -ItemType Directory -Force "models/imagegen" | Out-Null

hf download stable-diffusion-v1-5/stable-diffusion-v1-5 `
    v1-5-pruned-emaonly.safetensors `
    --local-dir models/imagegen

Write-Host ""
Write-Host "Model download complete."
Write-Host "models/imagegen/v1-5-pruned-emaonly.safetensors"
