param(
    [string]$Runtime = "win-x64",
    [string]$PythonVersion = "3.14.0",
    [string]$NssmVersion = "2.24"
)

# Paths
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = (Resolve-Path (Join-Path $scriptDir "..")).Path
$staging = Join-Path $scriptDir "staging"
$launcherProj = Join-Path $scriptDir "Launcher\Launcher.csproj"
$nsisScript = Join-Path $scriptDir "installer.nsi"
$publishDir = Join-Path $staging "launcher_publish"

Write-Host "Repo root:" $repoRoot
Write-Host "Staging dir:" $staging

# Prereqs
if (-not (Get-Command dotnet -ErrorAction SilentlyContinue)) {
    Write-Error "dotnet SDK not found in PATH. Install .NET SDK and re-run."
    exit 1
}

# Clean staging
if (Test-Path $staging) { Remove-Item $staging -Recurse -Force }
New-Item -Path $staging -ItemType Directory | Out-Null

# Publish single-file launcher
Write-Host "Publishing C# launcher (single-file)..."
dotnet publish $launcherProj -c Release -r $Runtime -p:PublishSingleFile=true -p:PublishTrimmed=false -p:IncludeNativeLibrariesForSelfExtract=true -o $publishDir
if ($LASTEXITCODE -ne 0) { Write-Error "dotnet publish failed"; exit 1 }

# Copy published launcher into staging root and ensure name 'launcher.exe'
$exe = Get-ChildItem -Path $publishDir -Filter *.exe -File -Recurse | Select-Object -First 1
if (-not $exe) { Write-Error "Published launcher exe not found"; exit 1 }
Copy-Item $exe.FullName (Join-Path $staging "launcher.exe") -Force
# Copy any additional published files (if present)
Get-ChildItem -Path $publishDir -Recurse | ForEach-Object {
    $relative = $_.FullName.Substring($publishDir.Length).TrimStart('\','/')
    $dest = Join-Path $staging $relative
    if ($_.PSIsContainer) {
        New-Item -Path $dest -ItemType Directory -Force | Out-Null
    } else {
        $destDir = Split-Path $dest -Parent
        if (-not (Test-Path $destDir)) { New-Item -Path $destDir -ItemType Directory -Force | Out-Null }
        Copy-Item $_.FullName $dest -Force
    }
}

# Copy server exe
$serverExe = Join-Path $repoRoot "build\src\Release\mcp_stdio_server.exe"
if (Test-Path $serverExe) {
    Copy-Item $serverExe $staging -Force
} else {
    Write-Warning "Server executable not found at $serverExe. Ensure you've built the project."
}

# Copy Python extension .pyd files (C++/pybind modules)
$builtPydDir = Join-Path $repoRoot "build\src\Release"
if (Test-Path $builtPydDir) {
    Get-ChildItem -Path $builtPydDir -Filter *.pyd -File -ErrorAction SilentlyContinue | ForEach-Object { Copy-Item $_.FullName $staging -Force }
}
# Also include any prebuilt cython .pyd placed in cython/
$cythonDir = Join-Path $repoRoot "cython"
if (Test-Path $cythonDir) {
    Get-ChildItem -Path $cythonDir -Filter *.pyd -File -ErrorAction SilentlyContinue | ForEach-Object { Copy-Item $_.FullName $staging -Force }
}

# Copy any required DLLs from built location (optional)
Get-ChildItem -Path $builtPydDir -Filter *.dll -File -ErrorAction SilentlyContinue | ForEach-Object { Copy-Item $_.FullName $staging -Force }

# Copy application icon into staging root so NSIS can embed/use it
$iconSrc = Join-Path $repoRoot "assets\icon\MnLLogo.ico"
if (Test-Path $iconSrc) {
    Copy-Item $iconSrc (Join-Path $staging "MnLLogo.ico") -Force
    Write-Host "Copied icon to staging: MnLLogo.ico"
} else {
    Write-Warning "Icon not found at $iconSrc"
}

# Download and extract NSSM (nssm.exe) into staging\nssm
$nssmZipName = "nssm-$NssmVersion.zip"
$nssmUrl = "https://nssm.cc/release/$nssmZipName"
$tmpNssmZip = Join-Path $env:TEMP $nssmZipName
$nssmStaging = Join-Path $staging "nssm"

if (-not (Test-Path $tmpNssmZip)) {
    Write-Host "Downloading NSSM $NssmVersion from $nssmUrl ..."
    try {
        Invoke-WebRequest -Uri $nssmUrl -OutFile $tmpNssmZip -UseBasicParsing -ErrorAction Stop
    } catch {
        Write-Warning "Failed to download NSSM: $_"
        Write-Warning "You may place a NSSM zip at $tmpNssmZip and re-run this script."
    }
} else {
    Write-Host "Using cached NSSM at $tmpNssmZip"
}

if (Test-Path $tmpNssmZip) {
    $extractDir = Join-Path $env:TEMP ("nssm_extract_" + [Guid]::NewGuid().ToString())
    New-Item -Path $extractDir -ItemType Directory | Out-Null
    Expand-Archive -Path $tmpNssmZip -DestinationPath $extractDir -Force
    # Find a win64 nssm.exe if present, otherwise pick first nssm.exe
    $found = Get-ChildItem -Path $extractDir -Filter nssm.exe -Recurse -File | Where-Object { $_.FullName -match "\\win64\\" } | Select-Object -First 1
    if (-not $found) {
        $found = Get-ChildItem -Path $extractDir -Filter nssm.exe -Recurse -File | Select-Object -First 1
    }
    if ($found) {
        New-Item -Path $nssmStaging -ItemType Directory -Force | Out-Null
        Copy-Item $found.FullName (Join-Path $nssmStaging "nssm.exe") -Force
        Write-Host "Copied nssm.exe to staging\nssm\nssm.exe"
    } else {
        Write-Warning "nssm.exe not found inside extracted NSSM archive."
    }
} else {
    Write-Warning "NSSM zip not available; service-install option will be missing."
}

# Download and extract Python embeddable
$embedName = "python-$PythonVersion-embed-amd64.zip"
$embedUrl = "https://www.python.org/ftp/python/$PythonVersion/$embedName"
$tmpZip = Join-Path $env:TEMP $embedName
$pythonStaging = Join-Path $staging "python"

if (-not (Test-Path $tmpZip)) {
    Write-Host "Downloading Python embeddable $embedUrl ..."
    try {
        Invoke-WebRequest -Uri $embedUrl -OutFile $tmpZip -UseBasicParsing -ErrorAction Stop
    } catch {
        Write-Warning "Failed to download Python embeddable: $_"
        Write-Warning "You may place a Python embeddable zip at $tmpZip and re-run this script."
    }
} else {
    Write-Host "Using cached embeddable at $tmpZip"
}

if (Test-Path $tmpZip) {
    Write-Host "Extracting Python embeddable..."
    Expand-Archive -Path $tmpZip -DestinationPath $pythonStaging -Force
} else {
    Write-Warning "Python embeddable not available; installer will be missing the embedded interpreter."
}

# Prepare staging structure expected by NSIS (staging/*)
Write-Host "Staging prepared at $staging"

# Run makensis to produce installer
if (Get-Command makensis -ErrorAction SilentlyContinue) {
    Push-Location $scriptDir
    Write-Host "Running makensis..."
    & makensis -V4 $nsisScript
    $rc = $LASTEXITCODE
    Pop-Location
    if ($rc -eq 0) {
        Write-Host "Installer built: $(Join-Path $scriptDir 'mcp_server_installer.exe')"
    } else {
        Write-Error "makensis failed (exit code $rc)"
        exit $rc
    }
} else {
    Write-Warning "makensis not found. Install NSIS and run 'makensis installer.nsi' inside the installer folder to build the installer."
    Write-Host "Staging contents are ready at: $staging"
}