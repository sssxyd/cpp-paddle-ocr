param(
    [string]$WorkspaceFolder = $PSScriptRoot + "\.."
)

# Set console encoding to support UTF-8
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
$OutputEncoding = [System.Text.Encoding]::UTF8

# Client resource compilation script

Write-Host "=== Compiling Client Resources ===" -ForegroundColor Green
Write-Host "Working Directory: $WorkspaceFolder" -ForegroundColor Yellow

# Use rc.exe path from environment variable
$rcPath = "$env:WIN_SDK_BIN\rc.exe"
if (-not (Test-Path $rcPath)) {
    Write-Error "Error: Cannot find rc.exe. Please ensure WIN_SDK_BIN environment variable is set. Path: $rcPath"
    exit 1
}
Write-Host "Found rc.exe: $rcPath" -ForegroundColor Green

# Ensure dist directory exists
$distDir = Join-Path $WorkspaceFolder "dist"
if (-not (Test-Path $distDir)) {
    New-Item -ItemType Directory -Path $distDir -Force | Out-Null
    Write-Host "Created dist directory: $distDir" -ForegroundColor Yellow
}

# Define file paths
$outputRes = Join-Path $WorkspaceFolder "dist\client_icon.res"
$resourcesDir = Join-Path $WorkspaceFolder "resources"
$inputRc = Join-Path $WorkspaceFolder "resources\client_icon.rc"

# Check if input file exists
if (-not (Test-Path $inputRc)) {
    Write-Error "Error: Cannot find resource file $inputRc"
    exit 1
}

# Build include paths
$includePaths = @("/I", $resourcesDir)

# Add Windows SDK header paths
if ($env:WIN_SDK_INCLUDE -and (Test-Path $env:WIN_SDK_INCLUDE)) {
    $includePaths += "/I", "$env:WIN_SDK_INCLUDE\um"
    $includePaths += "/I", "$env:WIN_SDK_INCLUDE\shared"
    Write-Host "Added SDK header paths: $env:WIN_SDK_INCLUDE" -ForegroundColor Yellow
} else {
    Write-Warning "Warning: WIN_SDK_INCLUDE environment variable not set or path does not exist"
}

# Execute resource compilation
$cmdArgs = @("/fo", $outputRes) + $includePaths + @($inputRc)
Write-Host "Executing command: $rcPath $($cmdArgs -join ' ')" -ForegroundColor Cyan

try {
    & $rcPath @cmdArgs
    if ($LASTEXITCODE -eq 0) {
        Write-Host "Success: Resource compilation completed $outputRes" -ForegroundColor Green
    } else {
        Write-Error "Resource compilation failed with exit code: $LASTEXITCODE"
        exit $LASTEXITCODE
    }
} catch {
    Write-Error "Error executing resource compilation: $($_.Exception.Message)"
    exit 1
}

Write-Host "=== Resource Compilation Completed ===" -ForegroundColor Green
