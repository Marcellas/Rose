[CmdletBinding()]
param(
    [ValidateSet('all', 'realvisxl-v5', 'juggernaut-xl', 'pony-v6')]
    [string]$Model = 'all',

    [switch]$Force
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# =============================================================================
# Rose SDXL checkpoint installer
# =============================================================================
#
# Installs the single-file SDXL checkpoints that Rose's LocalImageModelPreset
# expects. Downloads are resumable, verified with SHA-256, and only moved into
# their final Rose-owned filename after verification succeeds.
#
# Usage examples:
#   .\scripts\InstallRoseSdxlModels.ps1
#   .\scripts\InstallRoseSdxlModels.ps1 -Model realvisxl-v5
#   .\scripts\InstallRoseSdxlModels.ps1 -Model juggernaut-xl -Force
#
# The script intentionally does not install Python, Hugging Face CLI, or another
# package manager. Windows' curl.exe is sufficient and keeps this path small.
# =============================================================================

function Get-RoseProjectRoot {
    # This script lives in <Rose>/scripts, so its parent is the project root.
    return (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
}

function Get-FileSha256 {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Test-ExpectedHash {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,

        [Parameter(Mandatory = $true)]
        [string]$ExpectedSha256
    )

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        return $false
    }

    return (Get-FileSha256 -Path $Path) -eq $ExpectedSha256.ToLowerInvariant()
}

function Invoke-VerifiedCheckpointDownload {
    param(
        [Parameter(Mandatory = $true)]
        [string]$DisplayName,

        [Parameter(Mandatory = $true)]
        [string]$Url,

        [Parameter(Mandatory = $true)]
        [string]$DestinationDirectory,

        [Parameter(Mandatory = $true)]
        [string]$ExpectedSha256
    )

    New-Item -ItemType Directory -Force -Path $DestinationDirectory | Out-Null

    $destination = Join-Path $DestinationDirectory 'model.safetensors'
    $partial = "$destination.part"

    if ((Test-Path -LiteralPath $destination -PathType Leaf) -and -not $Force) {
        Write-Host "Checking existing $DisplayName checkpoint..."

        if (Test-ExpectedHash -Path $destination -ExpectedSha256 $ExpectedSha256) {
            Write-Host "  Already installed and verified: $destination" -ForegroundColor Green
            return
        }

        throw @"
$DisplayName already has a model.safetensors file, but its SHA-256 does not match
this installer's expected checkpoint.

Existing file:
  $destination

Use -Force if you intentionally want to replace it.
"@
    }

    if ($Force) {
        Remove-Item -LiteralPath $destination -Force -ErrorAction SilentlyContinue
    }

    $curl = Get-Command 'curl.exe' -ErrorAction SilentlyContinue
    if ($null -eq $curl) {
        throw 'curl.exe was not found. Current Windows 10/11 installations normally include it.'
    }

    Write-Host ""
    Write-Host "Installing $DisplayName" -ForegroundColor Cyan
    Write-Host "  Destination: $destination"
    Write-Host "  Partial file: $partial"
    Write-Host "  Existing partial downloads are resumed when the server permits it."

    # -C - resumes from an existing .part file. If there is no partial file it
    # starts at byte zero. --retry-all-errors helps with long multi-GB transfers.
    & $curl.Source `
        -L `
        --fail `
        --retry 5 `
        --retry-delay 3 `
        --retry-all-errors `
        -C - `
        -o $partial `
        $Url

    if ($LASTEXITCODE -ne 0) {
        throw "curl.exe failed while downloading $DisplayName (exit code $LASTEXITCODE). The .part file was retained for a later resume."
    }

    Write-Host "Verifying SHA-256..."
    $actualSha256 = Get-FileSha256 -Path $partial
    $expected = $ExpectedSha256.ToLowerInvariant()

    if ($actualSha256 -ne $expected) {
        throw @"
SHA-256 verification failed for $DisplayName.

Expected:
  $expected
Actual:
  $actualSha256

The partial file was kept at:
  $partial

Do not rename or use this file until the mismatch is resolved.
"@
    }

    # Move only after the full checkpoint has passed integrity verification.
    Move-Item -LiteralPath $partial -Destination $destination -Force

    Write-Host "  Installed and verified: $destination" -ForegroundColor Green
}

$projectRoot = Get-RoseProjectRoot
$imageModelRoot = Join-Path $projectRoot 'models\imagegen'

# Pinned upstream checkpoint metadata. Keeping this data here makes the install
# reproducible and prevents a future upstream filename change from silently
# changing what Rose loads under a stable preset ID.
$models = @(
    [pscustomobject]@{
        Id = 'realvisxl-v5'
        DisplayName = 'RealVisXL V5.0 (fp16)'
        Url = 'https://huggingface.co/SG161222/RealVisXL_V5.0/resolve/main/RealVisXL_V5.0_fp16.safetensors?download=true'
        Sha256 = '6a35a7855770ae9820a3c931d4964c3817b6d9e3c6f9c4dabb5b3a94e5643b80'
    },
    [pscustomobject]@{
        Id = 'juggernaut-xl'
        DisplayName = 'Juggernaut XL v9 RunDiffusion Photo v2'
        Url = 'https://huggingface.co/RunDiffusion/Juggernaut-XL-v9/resolve/main/Juggernaut-XL_v9_RunDiffusionPhoto_v2.safetensors?download=true'
        Sha256 = 'c9e3e68f89b8e38689e1097d4be4573cf308de4e3fd044c64ca697bdb4aa8bca'
    },
    [pscustomobject]@{
        Id = 'pony-v6'
        DisplayName = 'Pony Diffusion V6 XL'
        Url = 'https://huggingface.co/6chan/Pony-Diffusion-V6-XL/resolve/main/ponyDiffusionV6XL_v6StartWithThisOne.safetensors?download=true'
        Sha256 = '67ab2fd8ec439a89b3fedb15cc65f54336af163c7eb5e4f2acc98f090a29b0b3'
    }
)

$selectedModels = if ($Model -eq 'all') {
    $models
}
else {
    @($models | Where-Object { $_.Id -eq $Model })
}

if ($selectedModels.Count -eq 0) {
    throw "No installer metadata exists for model '$Model'."
}

Write-Host "Rose project root: $projectRoot"
Write-Host "Image model root: $imageModelRoot"
Write-Host ""
Write-Host "Selected checkpoint(s):"
foreach ($entry in $selectedModels) {
    Write-Host "  - $($entry.Id): $($entry.DisplayName)"
}

foreach ($entry in $selectedModels) {
    Invoke-VerifiedCheckpointDownload `
        -DisplayName $entry.DisplayName `
        -Url $entry.Url `
        -DestinationDirectory (Join-Path $imageModelRoot $entry.Id) `
        -ExpectedSha256 $entry.Sha256
}

Write-Host ""
Write-Host 'Rose SDXL model installation complete.' -ForegroundColor Green
Write-Host 'Use /imagemodel <id>, restart Rose, and confirm the startup model selection.'
