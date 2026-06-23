param(
    [ValidateSet("quote-coverage", "native-idf-assets", "native-idf-tls", "native-idf-generated", "compile-native-idf", "native-idf-rescue-bin", "flash-native-idf", "flash-native-idf-quote-data", "check-native-idf-size", "clean-build")]
    [string] $Target = "compile-native-idf",

    [string] $QuoteData = "data/quotes.sample.yaml",
    [string] $SerialPort = $env:QUOTES_CLOCK_SERIAL_PORT,
    [string] $EnvFile = ".env",
    [string] $NativeIdfDir = "firmware/native-idf",
    [string] $NativeIdfAppBin = "firmware/native-idf/build/quotes-clock-native.bin",
    [string] $NativeIdfRescueBin = "firmware/native-idf/build/quotes-clock-native-rescue.bin",
    [string] $NativeIdfMaxAppSize = "0x190000",
    [string] $NativeIdfAssets = "firmware/native-idf/main/generated/quotes_clock_assets.hpp",
    [string] $NativeIdfQuoteData = "firmware/native-idf/main/generated/quote_data.bin",
    [string] $NativeIdfTls = "firmware/native-idf/main/generated/tls_bootstrap.hpp",
    [string] $IdfPath = $env:IDF_PATH,
    [string] $IdfToolsPath = $env:IDF_TOOLS_PATH,
    [string] $IdfPythonEnvPath = $env:IDF_PYTHON_ENV_PATH
)

$ErrorActionPreference = "Stop"

function Import-DotEnv {
    param([string] $Path)

    if ($Path -eq $null -or $Path -eq "" -or -not (Test-Path -LiteralPath $Path)) {
        return
    }

    foreach ($line in Get-Content -LiteralPath $Path) {
        $trimmed = $line.Trim()
        if ($trimmed -eq "" -or $trimmed.StartsWith("#")) {
            continue
        }
        if ($trimmed -match '^(?:export\s+)?([A-Za-z_][A-Za-z0-9_]*)=(.*)$') {
            $key = $matches[1]
            $value = $matches[2].Trim()
            $doubleQuoted = $value.StartsWith('"') -and $value.EndsWith('"')
            if ($doubleQuoted -or ($value.StartsWith("'") -and $value.EndsWith("'"))) {
                $value = $value.Substring(1, $value.Length - 2)
            }
            if ($doubleQuoted) {
                $value = $value -replace '\\"', '"' -replace '\\n', "`n" -replace '\\r', "`r" -replace '\\t', "`t"
            }
            if ((Get-Item -Path "env:$key" -ErrorAction SilentlyContinue) -eq $null) {
                Set-Item -Path "env:$key" -Value $value
            }
        }
    }
}

Import-DotEnv $EnvFile
if (-not $PSBoundParameters.ContainsKey("SerialPort") -and $env:QUOTES_CLOCK_SERIAL_PORT -ne $null -and $env:QUOTES_CLOCK_SERIAL_PORT -ne "") {
    $SerialPort = $env:QUOTES_CLOCK_SERIAL_PORT
}
function Invoke-Checked {
    param([scriptblock] $Command)
    & $Command
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}

function Invoke-IdfPy {
    param([string[]] $Arguments)

    $idfCommand = Get-Command idf.py -ErrorAction SilentlyContinue
    if ($idfCommand) {
        & $idfCommand @Arguments
        if ($LASTEXITCODE -ne 0) {
            exit $LASTEXITCODE
        }
        return
    }

    $resolvedIdfPath = $IdfPath
    if ($resolvedIdfPath -eq $null -or $resolvedIdfPath -eq "") {
        $resolvedIdfPath = Get-ChildItem -LiteralPath "C:\esp" -Directory -ErrorAction SilentlyContinue |
            Where-Object { Test-Path (Join-Path $_.FullName "esp-idf/tools/idf.py") } |
            Sort-Object -Property @{
                Expression = {
                    try {
                        [version] ($_.Name -replace '^v', '')
                    } catch {
                        [version] "0.0.0"
                    }
                }
                Descending = $true
            } |
            Select-Object -First 1 -ExpandProperty FullName

        if ($resolvedIdfPath -ne $null -and $resolvedIdfPath -ne "") {
            $resolvedIdfPath = Join-Path $resolvedIdfPath "esp-idf"
        }
    }

    if ($resolvedIdfPath -eq $null -or $resolvedIdfPath -eq "") {
        throw "idf.py is not on PATH and ESP-IDF was not found under C:\esp. Open the ESP-IDF PowerShell shortcut, run export.ps1 for ESP-IDF v6.0.x, or pass -IdfPath C:\esp\v6.0.1\esp-idf."
    }

    $idfScript = Join-Path $resolvedIdfPath "tools/idf.py"
    if (-not (Test-Path $idfScript)) {
        throw "Could not find idf.py at $idfScript"
    }

    $python = "python"
    $resolvedPythonEnvPath = $IdfPythonEnvPath
    $idfVersion = Split-Path (Split-Path $resolvedIdfPath -Parent) -Leaf
    if ($resolvedPythonEnvPath -eq $null -or $resolvedPythonEnvPath -eq "") {
        $candidatePythonEnv = Join-Path "C:\Espressif\tools\python" "$idfVersion/venv"
        if (Test-Path $candidatePythonEnv) {
            $resolvedPythonEnvPath = $candidatePythonEnv
        }
    }

    if ($resolvedPythonEnvPath -ne $null -and $resolvedPythonEnvPath -ne "") {
        $venvPython = Join-Path $resolvedPythonEnvPath "Scripts/python.exe"
        if (Test-Path $venvPython) {
            $python = $venvPython
        }
    }

    $resolvedToolsPath = $IdfToolsPath
    if (($resolvedToolsPath -eq $null -or $resolvedToolsPath -eq "") -and (Test-Path "C:\Espressif\tools")) {
        $resolvedToolsPath = "C:\Espressif\tools"
    }

    $env:IDF_PATH = $resolvedIdfPath
    if ($resolvedToolsPath -ne $null -and $resolvedToolsPath -ne "") {
        $env:IDF_TOOLS_PATH = $resolvedToolsPath
    }
    if ($resolvedPythonEnvPath -ne $null -and $resolvedPythonEnvPath -ne "") {
        $env:IDF_PYTHON_ENV_PATH = $resolvedPythonEnvPath
    }
    if ($env:ESP_IDF_VERSION -eq $null -or $env:ESP_IDF_VERSION -eq "") {
        $env:ESP_IDF_VERSION = $idfVersion -replace '^v', ''
    }

    $usedEimProfile = $false
    if ($resolvedToolsPath -ne $null -and $resolvedToolsPath -ne "") {
        $eimProfile = Join-Path $resolvedToolsPath "Microsoft.$idfVersion.PowerShell_profile.ps1"
        if (Test-Path $eimProfile) {
            $profileExports = & $eimProfile -e 6>&1
            $profilePath = $null
            $profileSystemPath = $null
            foreach ($line in $profileExports) {
                if ($line -match '^([^=]+)=(.*)$') {
                    $key = $matches[1]
                    $value = $matches[2]
                    if ($key -eq "PATH") {
                        $profilePath = $value
                    } elseif ($key -eq "SYSTEM_PATH") {
                        $profileSystemPath = $value
                    } else {
                        Set-Item -Path "env:$key" -Value $value
                    }
                }
            }
            if ($profilePath -ne $null -and $profilePath -ne "") {
                if ($profileSystemPath -ne $null -and $profileSystemPath -ne "") {
                    $env:PATH = "$profilePath;$profileSystemPath"
                } else {
                    $env:PATH = "$profilePath;$env:PATH"
                }
            }
            $usedEimProfile = $true
        }
    }

    if ($resolvedToolsPath -ne $null -and $resolvedToolsPath -ne "") {
        if ($env:IDF_COMPONENT_LOCAL_STORAGE_URL -eq $null -or $env:IDF_COMPONENT_LOCAL_STORAGE_URL -eq "") {
            $env:IDF_COMPONENT_LOCAL_STORAGE_URL = "file://$resolvedToolsPath"
        }

        if ($env:ESP_ROM_ELF_DIR -eq $null -or $env:ESP_ROM_ELF_DIR -eq "") {
            $romElfRoot = Join-Path $resolvedToolsPath "esp-rom-elfs"
            $romElfDir = Get-ChildItem -LiteralPath $romElfRoot -Directory -ErrorAction SilentlyContinue |
                Sort-Object -Property Name -Descending |
                Select-Object -First 1 -ExpandProperty FullName
            if ($romElfDir -ne $null -and $romElfDir -ne "") {
                $env:ESP_ROM_ELF_DIR = $romElfDir
            }
        }

        if ($env:OPENOCD_SCRIPTS -eq $null -or $env:OPENOCD_SCRIPTS -eq "") {
            $openOcdRoot = Join-Path $resolvedToolsPath "openocd-esp32"
            $openOcdScripts = Get-ChildItem -LiteralPath $openOcdRoot -Directory -ErrorAction SilentlyContinue |
                Sort-Object -Property Name -Descending |
                ForEach-Object { Join-Path $_.FullName "openocd-esp32/share/openocd/scripts" } |
                Where-Object { Test-Path $_ } |
                Select-Object -First 1
            if ($openOcdScripts -ne $null -and $openOcdScripts -ne "") {
                $env:OPENOCD_SCRIPTS = $openOcdScripts
            }
        }
    }

    $activateScript = Join-Path $resolvedIdfPath "tools/activate.py"
    if ((-not $usedEimProfile) -and (Test-Path $activateScript)) {
        $idfExports = & $python $activateScript --export
        if ($LASTEXITCODE -ne 0) {
            exit $LASTEXITCODE
        }
        if ($idfExports -ne $null -and $idfExports -ne "") {
            . $idfExports
        }
    }

    & $python $idfScript @Arguments
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}

function Get-IdfPython {
    if ($env:IDF_PYTHON_ENV_PATH -ne $null -and $env:IDF_PYTHON_ENV_PATH -ne "") {
        $venvPython = Join-Path $env:IDF_PYTHON_ENV_PATH "Scripts/python.exe"
        if (Test-Path -LiteralPath $venvPython) {
            return $venvPython
        }
        $venvPython = Join-Path $env:IDF_PYTHON_ENV_PATH "bin/python"
        if (Test-Path -LiteralPath $venvPython) {
            return $venvPython
        }
    }
    return "python"
}

function Build-NativeIdfAssets {
    Invoke-Checked {
        uv run python tools/generate_native_assets.py `
            --input $QuoteData `
            --output $NativeIdfAssets `
            --quote-output $NativeIdfQuoteData `
            --omit-quote-arrays
    }
}

function Build-NativeIdfTls {
    Invoke-Checked {
        uv run python tools/generate_native_tls.py `
            --output $NativeIdfTls `
            --env-file $EnvFile
    }
}

function Build-NativeIdfGenerated {
    Build-NativeIdfAssets
    Build-NativeIdfTls
}

function Compile-NativeIdf {
    Build-NativeIdfGenerated
    $env:CMAKE_POLICY_VERSION_MINIMUM = "3.5"
    Push-Location $NativeIdfDir
    try {
        Invoke-IdfPy @("build")
    } finally {
        Pop-Location
    }
}

function Build-NativeIdfRescueBin {
    Compile-NativeIdf
    $python = Get-IdfPython
    Invoke-Checked {
        & $python tools/package_native_rescue.py `
            --project-dir $NativeIdfDir `
            --quote-data $NativeIdfQuoteData `
            --output $NativeIdfRescueBin
    }
}

function Flash-NativeIdf {
    if ($SerialPort -eq $null -or $SerialPort -eq "") {
        throw "Serial port is required. Pass -SerialPort COM6 or set QUOTES_CLOCK_SERIAL_PORT."
    }
    Compile-NativeIdf
    Push-Location $NativeIdfDir
    try {
        Invoke-IdfPy @("-p", $SerialPort, "flash")
    } finally {
        Pop-Location
    }
}

function Flash-NativeIdfQuoteData {
    if ($SerialPort -eq $null -or $SerialPort -eq "") {
        throw "Serial port is required. Pass -SerialPort COM6 or set QUOTES_CLOCK_SERIAL_PORT."
    }
    Build-NativeIdfAssets
    $quoteDataPath = (Resolve-Path -LiteralPath $NativeIdfQuoteData).Path
    Push-Location $NativeIdfDir
    try {
        Invoke-IdfPy @("partition-table")
        $partTool = Join-Path $env:IDF_PATH "components/partition_table/parttool.py"
        if (-not (Test-Path -LiteralPath $partTool)) {
            throw "Could not find parttool.py at $partTool"
        }
        $python = Get-IdfPython
        Invoke-Checked {
            & $python $partTool --port $SerialPort write_partition --partition-name quote_data --input $quoteDataPath
        }
    } finally {
        Pop-Location
    }
}

switch ($Target) {
    "quote-coverage" {
        Invoke-Checked { uv run python tools/validate_quotes.py $QuoteData }
        Invoke-Checked { uv run python tools/report_quote_coverage.py $QuoteData }
        Invoke-Checked { uv run python tools/report_display_text_glyphs.py $QuoteData }
    }
    "native-idf-assets" {
        Build-NativeIdfAssets
    }
    "native-idf-tls" {
        Build-NativeIdfTls
    }
    "native-idf-generated" {
        Build-NativeIdfGenerated
    }
    "compile-native-idf" {
        Compile-NativeIdf
    }
    "native-idf-rescue-bin" {
        Build-NativeIdfRescueBin
    }
    "flash-native-idf" {
        Flash-NativeIdf
    }
    "flash-native-idf-quote-data" {
        Flash-NativeIdfQuoteData
    }
    "check-native-idf-size" {
        Invoke-Checked {
            uv run python tools/check_binary_size.py `
                --file $NativeIdfAppBin `
                --max-bytes $NativeIdfMaxAppSize
        }
    }
    "clean-build" {
        Remove-Item -LiteralPath "firmware/native-idf/build" -Recurse -Force -ErrorAction SilentlyContinue
        Remove-Item -LiteralPath "firmware/native-idf/managed_components" -Recurse -Force -ErrorAction SilentlyContinue
        Remove-Item -LiteralPath "firmware/native-idf/main/generated" -Recurse -Force -ErrorAction SilentlyContinue
        Remove-Item -LiteralPath "firmware/native-idf/dependencies.lock" -Force -ErrorAction SilentlyContinue
        Remove-Item -LiteralPath "firmware/native-idf/sdkconfig" -Force -ErrorAction SilentlyContinue
        Remove-Item -LiteralPath "firmware/native-idf/sdkconfig.esp32dev" -Force -ErrorAction SilentlyContinue
    }
}
