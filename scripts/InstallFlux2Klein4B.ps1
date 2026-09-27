param(
    [string]$Destination = ".\models\imagegen\flux2-klein-4b"
)

$ErrorActionPreference = "Stop"

function Download-VerifiedFile {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Url,

        [Parameter(Mandatory = $true)]
        [string]$OutputPath,

        [Parameter(Mandatory = $true)]
        [string]$ExpectedSha256
    )

    $directory = Split-Path -Parent $OutputPath
    New-Item -ItemType Directory -Force -Path $directory | Out-Null

    Write-Host ""
    Write-Host "Downloading:"
    Write-Host "  $Url"
    Write-Host "to:"
    Write-Host "  $OutputPath"

    # curl.exe is present on current supported Windows versions.
    # -L follows Hugging Face/Xet redirects.
    # -C - resumes an interrupted partial download.
    # --fail turns HTTP errors into a non-zero exit status.
    & curl.exe `
        -L `
        --fail `
        --retry 5 `
        --retry-delay 5 `
        -C - `
        --output $OutputPath `
        $Url

    if ($LASTEXITCODE -ne 0) {
        throw "Download failed with curl exit code $LASTEXITCODE."
    }

    Write-Host "Verifying SHA-256..."
    $actual = (Get-FileHash -Algorithm SHA256 -Path $OutputPath).Hash.ToLowerInvariant()
    $expected = $ExpectedSha256.ToLowerInvariant()

    if ($actual -ne $expected) {
        throw @"
SHA-256 mismatch for:
  $OutputPath

Expected:
  $expected

Actual:
  $actual

Delete the file and retry. Rose should not use an unverified model artifact.
"@
    }

    Write-Host "Verified: $OutputPath"
}

$Destination = [System.IO.Path]::GetFullPath($Destination)
New-Item -ItemType Directory -Force -Path $Destination | Out-Null

Write-Host "Installing Rose FLUX.2 Klein 4B assets into:"
Write-Host "  $Destination"

# Quality-first diffusion quantization.
#
# Source:
#   unsloth/FLUX.2-klein-4B-GGUF
#
# Original filename:
#   flux-2-klein-4b-Q8_0.gguf
#
# Rose stable local name:
#   diffusion.gguf
Download-VerifiedFile `
    -Url "https://huggingface.co/unsloth/FLUX.2-klein-4B-GGUF/resolve/main/flux-2-klein-4b-Q8_0.gguf?download=true" `
    -OutputPath (Join-Path $Destination "diffusion.gguf") `
    -ExpectedSha256 "24a812e8f9b640e21c784164ea48571d8017ca22873696f4badeeabb006d509c"

# Qwen3-4B text encoder.
#
# Source:
#   unsloth/Qwen3-4B-GGUF
#
# Original filename:
#   Qwen3-4B-Q4_K_M.gguf
#
# Rose stable local name:
#   text_encoder.gguf
Download-VerifiedFile `
    -Url "https://huggingface.co/unsloth/Qwen3-4B-GGUF/resolve/main/Qwen3-4B-Q4_K_M.gguf?download=true" `
    -OutputPath (Join-Path $Destination "text_encoder.gguf") `
    -ExpectedSha256 "f6f851777709861056efcdad3af01da38b31223a3ba26e61a4f8bf3a2195813a"

# FLUX.2 full encoder + distilled small decoder.
#
# Source:
#   black-forest-labs/FLUX.2-small-decoder
#
# Original filename:
#   full_encoder_small_decoder.safetensors
#
# Rose stable local name:
#   ae.safetensors
Download-VerifiedFile `
    -Url "https://huggingface.co/black-forest-labs/FLUX.2-small-decoder/resolve/main/full_encoder_small_decoder.safetensors?download=true" `
    -OutputPath (Join-Path $Destination "ae.safetensors") `
    -ExpectedSha256 "ea4273f02d1fafbf8e1d1c2cf6018ed8748652eb0bf34f2dd91171f16f15ab62"

Write-Host ""
Write-Host "FLUX.2 Klein 4B model assets are installed and verified."
Write-Host ""
Write-Host "Rose expects:"
Write-Host "  diffusion.gguf"
Write-Host "  text_encoder.gguf"
Write-Host "  ae.safetensors"
Write-Host ""
Write-Host "Next Rose launch should report:"
Write-Host "  Image model selected: FLUX.2 Klein 4B"
