# test_permutations.ps1 - Stress test core examples across flag permutations
#
# Usage:
#   .\scripts\test_permutations.ps1                   # Full stress run (2s each)
#   .\scripts\test_permutations.ps1 -Quick            # Quick run (1s each)
#   .\scripts\test_permutations.ps1 -Example texture  # Test single example
#   .\scripts\test_permutations.ps1 -Duration 5       # Run each test for 5 seconds
#   .\scripts\test_permutations.ps1 -SkipPerf         # Skip performance logging runs
#   .\scripts\test_permutations.ps1 -SkipBuild        # Skip build step
#   .\scripts\test_permutations.ps1 -SkipInternalFallbacks  # Skip internal extension fallback tests
#
# Exit codes:
#   0 = All tests passed
#   1 = One or more tests failed

param(
    [switch]$Quick,
    [string]$Example = "",
    [float]$Duration = 2.0,
    [float]$PerfDuration = 5.0,
    [switch]$SkipPerf,
    [switch]$SkipBuild,
    [switch]$SkipInternalFallbacks,  # Skip internal extension fallback tests (run by default)
    [string]$CoreConfig = "Debug",
    [string]$PerfConfig = "Release"
)

$ErrorActionPreference = "Stop"

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot ".."))
$BuildDir = Join-Path $RepoRoot "build"
$LogDir = Join-Path $BuildDir "test_logs"

# Configuration
$CoreExamples = @(
    "triangle",             # swapchain + basic graphics
    "pipelines",            # pipeline churn + render pass usage
    "descriptorsets",       # descriptor pools/updates + MSAA resolve
    "texture",              # staging uploads + sampling
    "computeparticles",     # compute queue + storage buffers
    "dynamicuniformbuffer"  # dynamic offsets + bind updates
)
$PerfExamples = @(
    "gltf_renderer"         # heavier scene for perf logging
)

# Flag permutations (full cross product)
# Validation always enabled, test all combinations of fallback flags
$FlagOptions = @(
    @("--validation"),                      # Always present
    @("", "--no-timeline-semaphore"),       # With or without fence-based sync
    @("", "--legacy-renderpass"),           # With or without legacy renderpass
    @("", "--single-queue")                 # With or without single queue
)

# Quick mode: Only test flags that are RELEVANT to each example
# This dramatically reduces redundant tests while maintaining coverage
#
# Flag relevance:
#   --no-timeline-semaphore: Affects staging uploads (DEVICE_LOCAL buffers)
#   --legacy-renderpass: Affects MSAA/resolve attachments, subpass structure
#   --single-queue: Only affects examples using compute queue
#
$QuickExampleFlags = @{
    # Tier 1: Full matrix - comprehensive example with MSAA + staging
    "descriptorsets" = @(      # MSAA w/ resolve (legacy RP), textures (staging)
        @("--validation"),
        @("--validation", "--legacy-renderpass"),
        @("--validation", "--no-timeline-semaphore"),
        @("--validation", "--no-timeline-semaphore", "--legacy-renderpass")
    )

    # Tier 2: Staging-focused (DEVICE_LOCAL buffers with upload)
    "texture" = @(             # KTX staging upload path
        @("--validation"),
        @("--validation", "--no-timeline-semaphore")
    )
    "computeparticles" = @(    # Storage buffer staging (NOTE: uses graphics queue, not async compute)
        @("--validation"),
        @("--validation", "--no-timeline-semaphore")
    )

    # Tier 3: Baseline only - HOST_VISIBLE buffers, single queue, simple RP
    "triangle" = @(
        @("--validation")
    )
    "pipelines" = @(           # Multiple pipelines, test legacy RP variant
        @("--validation"),
        @("--validation", "--legacy-renderpass")
    )
    "dynamicuniformbuffer" = @(
        @("--validation")
    )
}

function Get-FlagPermutations {
    param([object[]]$Options)
    # Use unary comma to prevent PowerShell from flattening the empty array
    $results = ,@()
    foreach ($opts in $Options) {
        $next = @()
        foreach ($prefix in $results) {
            foreach ($opt in $opts) {
                $next += ,($prefix + $opt)
            }
        }
        $results = $next
    }
    return $results
}

function Find-ExampleExe {
    param([string]$ExampleName, [string]$Config, [bool]$AllowFallback = $true)
    $base = Join-Path $BuildDir "examples\$ExampleName"
    if (-not (Test-Path $base)) {
        return $null
    }
    $candidate = Join-Path $base "$Config\$ExampleName.exe"
    if (Test-Path $candidate) {
        return $candidate
    }
    if ($AllowFallback) {
        foreach ($cfg in @("Debug", "Release", "RelWithDebInfo", "MinSizeRel")) {
            $alt = Join-Path $base "$cfg\$ExampleName.exe"
            if (Test-Path $alt) {
                return $alt
            }
        }
        $fallback = Get-ChildItem -Path $base -Recurse -Filter "$ExampleName.exe" -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($null -ne $fallback) {
            return $fallback.FullName
        }
    }
    return $null
}

# Validation error patterns
$ValidationPatterns = @(
    "Validation Error:",
    "VUID-",
    "VK_ERROR_",
    "ERROR: VALIDATION"
)

function Test-ValidationError {
    param([string]$Output)
    foreach ($pattern in $ValidationPatterns) {
        if ($Output -match [regex]::Escape($pattern)) {
            return $true
        }
    }
    return $false
}

function Get-ValidationErrorSummary {
    param([string]$Output)
    $lines = $Output -split "`n"
    $errors = @()
    foreach ($line in $lines) {
        foreach ($pattern in $ValidationPatterns) {
            if ($line -match [regex]::Escape($pattern)) {
                $errors += $line.Trim()
                break
            }
        }
    }
    return $errors
}

function Get-FpsStats {
    param([string]$Output)

    # Try to parse the new [PERF] SUMMARY format first
    # Format: [PERF] SUMMARY: avg_fps=X min_fps=X max_fps=X fps_1_low=X fps_01_low=X avg_ms=X
    foreach ($line in ($Output -split "`n")) {
        if ($line -match '\[PERF\] SUMMARY:') {
            $stats = @{}
            if ($line -match 'avg_fps=([0-9.]+)') {
                $stats.Average = [Math]::Round([double]::Parse($Matches[1], [Globalization.CultureInfo]::InvariantCulture), 1)
            }
            if ($line -match 'min_fps=([0-9.]+)') {
                $stats.Minimum = [Math]::Round([double]::Parse($Matches[1], [Globalization.CultureInfo]::InvariantCulture), 1)
            }
            if ($line -match 'max_fps=([0-9.]+)') {
                $stats.Maximum = [Math]::Round([double]::Parse($Matches[1], [Globalization.CultureInfo]::InvariantCulture), 1)
            }
            if ($line -match 'fps_1_low=([0-9.]+)') {
                $stats.Low1Pct = [Math]::Round([double]::Parse($Matches[1], [Globalization.CultureInfo]::InvariantCulture), 1)
            }
            if ($line -match 'fps_01_low=([0-9.]+)') {
                $stats.Low01Pct = [Math]::Round([double]::Parse($Matches[1], [Globalization.CultureInfo]::InvariantCulture), 1)
            }
            if ($line -match 'avg_ms=([0-9.]+)') {
                $stats.AvgMs = [Math]::Round([double]::Parse($Matches[1], [Globalization.CultureInfo]::InvariantCulture), 2)
            }
            if ($stats.Count -gt 0) {
                return $stats
            }
        }
    }

    # Fallback: try legacy format "(XXX FPS)"
    $fpsValues = @()
    foreach ($line in ($Output -split "`n")) {
        if ($line -match "\(([0-9]+(\.[0-9]+)?) FPS\)") {
            $fpsValues += [double]::Parse($Matches[1], [Globalization.CultureInfo]::InvariantCulture)
        }
    }
    if ($fpsValues.Count -eq 0) {
        return $null
    }
    $avg = ($fpsValues | Measure-Object -Average).Average
    $min = ($fpsValues | Measure-Object -Minimum).Minimum
    $max = ($fpsValues | Measure-Object -Maximum).Maximum
    return @{
        Average = [Math]::Round($avg, 2)
        Minimum = [Math]::Round($min, 2)
        Maximum = [Math]::Round($max, 2)
    }
}

function Invoke-TestRun {
    param(
        [string]$ExePath,
        [string[]]$TestArgs,
        [int]$TimeoutMs,
        [string]$TestName,
        [string]$LogFile,
        [hashtable]$EnvVars = @{}
    )

    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $ExePath
    $psi.Arguments = $TestArgs -join " "
    $psi.UseShellExecute = $false
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    # Note: Don't use CreateNoWindow - SDL apps need a window for event processing
    $psi.WorkingDirectory = Split-Path $ExePath -Parent

    # Add environment variables
    foreach ($key in $EnvVars.Keys) {
        $psi.EnvironmentVariables[$key] = $EnvVars[$key]
    }

    $process = New-Object System.Diagnostics.Process
    $process.StartInfo = $psi

    $stdout = New-Object System.Text.StringBuilder
    $stderr = New-Object System.Text.StringBuilder

    $stdoutId = "stdout_$([guid]::NewGuid().ToString())"
    $stderrId = "stderr_$([guid]::NewGuid().ToString())"

    try {
        Register-ObjectEvent -InputObject $process -EventName OutputDataReceived -SourceIdentifier $stdoutId -Action {
            if (-not [String]::IsNullOrEmpty($EventArgs.Data)) {
                $Event.MessageData.AppendLine($EventArgs.Data)
            }
        } -MessageData $stdout | Out-Null

        Register-ObjectEvent -InputObject $process -EventName ErrorDataReceived -SourceIdentifier $stderrId -Action {
            if (-not [String]::IsNullOrEmpty($EventArgs.Data)) {
                $Event.MessageData.AppendLine($EventArgs.Data)
            }
        } -MessageData $stderr | Out-Null

        $process.Start() | Out-Null
        $process.BeginOutputReadLine()
        $process.BeginErrorReadLine()

        $exited = $process.WaitForExit($TimeoutMs)

        if (-not $exited) {
            # Process didn't exit in time - kill it
            try {
                $process.Kill()
                $process.WaitForExit(1000)  # Wait up to 1s for kill to complete
            } catch {
                # Process may have already exited
            }
        } else {
            # Process exited - wait for async output to complete (per MSDN docs)
            $process.WaitForExit()
        }

        # Cancel async reads and wait for events to flush
        try { $process.CancelOutputRead() } catch {}
        try { $process.CancelErrorRead() } catch {}

        # Give async events time to be processed
        for ($i = 0; $i -lt 10; $i++) {
            Start-Sleep -Milliseconds 50
            [System.Threading.Thread]::Sleep(10)
        }

        $output = $stdout.ToString() + "`n" + $stderr.ToString()

        if ($LogFile -ne "") {
            $logContent = @"
Test: $TestName
Executable: $ExePath
Args: $($TestArgs -join ' ')
Timestamp: $(Get-Date -Format "yyyy-MM-dd HH:mm:ss")
Exit Code: $($process.ExitCode)
Exited: $exited
----------------------------------------
STDOUT:
$($stdout.ToString())
----------------------------------------
STDERR:
$($stderr.ToString())
"@
            Set-Content -Path $LogFile -Value $logContent
        }

        return @{
            Exited = $exited
            ExitCode = $process.ExitCode
            Output = $output
        }
    }
    finally {
        Unregister-Event -SourceIdentifier $stdoutId -ErrorAction SilentlyContinue
        Unregister-Event -SourceIdentifier $stderrId -ErrorAction SilentlyContinue
        Remove-Event -SourceIdentifier $stdoutId -ErrorAction SilentlyContinue
        Remove-Event -SourceIdentifier $stderrId -ErrorAction SilentlyContinue
        if (-not $process.HasExited) {
            try { $process.Kill() } catch {}
        }
        $process.Dispose()
    }
}

if ($Quick) {
    $Duration = 0.75
    $PerfDuration = 2.0
}

# Filter examples if specified
if ($Example -ne "") {
    $CoreExamples = @($Example)
    $PerfExamples = @($Example)
}

if (-not (Test-Path $BuildDir)) {
    New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null
}

$BuiltConfigs = @{}
function Ensure-Build {
    param([string]$Config)
    if ($SkipBuild) {
        return $false
    }
    if ($BuiltConfigs.ContainsKey($Config)) {
        return $true
    }
    Write-Host "Building ($Config)..." -ForegroundColor Cyan
    & cmake --build $BuildDir --config $Config
    if ($LASTEXITCODE -ne 0) {
        throw "Build failed for configuration: $Config"
    }
    $BuiltConfigs[$Config] = $true
    return $true
}

# Generate permutations (full mode uses cross-product, Quick mode uses per-example)
$Permutations = Get-FlagPermutations -Options $FlagOptions

# Create timestamped log directory
$Timestamp = Get-Date -Format "yyyyMMdd_HHmmss"
$CurrentLogDir = "$LogDir\$Timestamp"
New-Item -ItemType Directory -Force -Path $CurrentLogDir | Out-Null

# Calculate total tests based on mode
if ($Quick) {
    $TotalTests = 0
    foreach ($ex in $CoreExamples) {
        if ($QuickExampleFlags.ContainsKey($ex)) {
            $TotalTests += $QuickExampleFlags[$ex].Count
        } else {
            $TotalTests += 1  # Fallback: just validation
        }
    }
} else {
    $TotalTests = $CoreExamples.Count * $Permutations.Count
}

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "HinaVK Stress Test Suite" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "Core examples: $($CoreExamples -join ', ')"
if ($Quick) {
    Write-Host "Mode: QUICK (smart flag matrix - $TotalTests tests)" -ForegroundColor Yellow
} else {
    Write-Host "Mode: FULL ($($Permutations.Count) permutations x $($CoreExamples.Count) examples)"
}
Write-Host "Duration per test: ${Duration}s"
Write-Host "Core config: $CoreConfig"
if (-not $SkipPerf) {
    Write-Host "Perf examples: $($PerfExamples -join ', ')"
    Write-Host "Perf duration: ${PerfDuration}s"
    Write-Host "Perf config: $PerfConfig"
}
Write-Host "Log directory: $CurrentLogDir"
Write-Host ""
$PassedTests = 0
$FailedTests = 0
$FailedList = @()
$ValidationErrorTests = @()
$PerfResults = @()

$TestNum = 0
foreach ($ExampleName in $CoreExamples) {
    $ExePath = Find-ExampleExe -ExampleName $ExampleName -Config $CoreConfig -AllowFallback $true
    if ($null -eq $ExePath -or -not (Test-Path $ExePath)) {
        if (-not $SkipBuild) {
            Ensure-Build -Config $CoreConfig | Out-Null
            $ExePath = Find-ExampleExe -ExampleName $ExampleName -Config $CoreConfig -AllowFallback $true
        }
    }
    if ($null -eq $ExePath -or -not (Test-Path $ExePath)) {
        Write-Host "ERROR: Example not found: $ExampleName (config: $CoreConfig)" -ForegroundColor Red
        Write-Host "Run 'cmake --build build --config $CoreConfig' first" -ForegroundColor Yellow
        exit 1
    }

    # Select permutations: Quick mode uses smart per-example flags, Full mode uses cross-product
    if ($Quick -and $QuickExampleFlags.ContainsKey($ExampleName)) {
        $ExamplePermutations = $QuickExampleFlags[$ExampleName]
    } elseif ($Quick) {
        # Fallback for Quick mode: just validation
        $ExamplePermutations = @(,@("--validation"))
    } else {
        $ExamplePermutations = $Permutations
    }

    foreach ($Flags in $ExamplePermutations) {
        $TestNum++
        $FlagsStr = $Flags -join " "
        $TestName = "$ExampleName [$FlagsStr]"
        $SafeTestName = "${ExampleName}_test${TestNum}"
        $LogFile = "$CurrentLogDir\$SafeTestName.log"

        Write-Host "[$TestNum/$TotalTests] Testing: $ExampleName" -ForegroundColor Yellow
        Write-Host "  Flags: $FlagsStr" -ForegroundColor Gray

        $AllArgs = $Flags + @("--duration=$Duration")
        $TimeoutMs = [int](($Duration + 5) * 1000)

        try {
            $result = Invoke-TestRun -ExePath $ExePath -TestArgs $AllArgs -TimeoutMs $TimeoutMs -TestName $TestName -LogFile $LogFile
            $exited = $result.Exited
            $output = $result.Output

            # Check for validation errors
            $hasValidationError = Test-ValidationError -Output $output
            $validationErrors = @()
            if ($hasValidationError) {
                $validationErrors = Get-ValidationErrorSummary -Output $output
            }

            if (-not $exited) {
                Write-Host "  TIMEOUT (killed after $($Duration + 5)s)" -ForegroundColor Red
                $FailedTests++
                $FailedList += "$TestName (TIMEOUT)"
            }
            elseif ($hasValidationError) {
                Write-Host "  FAILED (validation errors detected)" -ForegroundColor Red
                Write-Host "    Log: $LogFile" -ForegroundColor Gray
                foreach ($err in $validationErrors | Select-Object -First 3) {
                    Write-Host "    $err" -ForegroundColor DarkRed
                }
                if ($validationErrors.Count -gt 3) {
                    Write-Host "    ... and $($validationErrors.Count - 3) more errors" -ForegroundColor DarkRed
                }
                $FailedTests++
                $FailedList += "$TestName (VALIDATION)"
                $ValidationErrorTests += @{ Name = $TestName; LogFile = $LogFile; Errors = $validationErrors }
            }
            elseif ($result.ExitCode -eq 0) {
                Write-Host "  PASSED" -ForegroundColor Green
                $PassedTests++
            } else {
                Write-Host "  FAILED (exit code: $($result.ExitCode))" -ForegroundColor Red
                Write-Host "    Log: $LogFile" -ForegroundColor Gray
                $FailedTests++
                $FailedList += $TestName
            }
        } catch {
            Write-Host "  FAILED (exception: $_)" -ForegroundColor Red
            $FailedTests++
            $FailedList += $TestName
        }
    }
}

# Internal fallback tests - test legacy code paths via environment variables (runs by default)
if (-not $SkipInternalFallbacks) {
    Write-Host ""
    Write-Host "========================================" -ForegroundColor Cyan
    Write-Host "Internal Fallback Tests" -ForegroundColor Cyan
    Write-Host "========================================" -ForegroundColor Cyan
    Write-Host "Testing legacy code paths (sync2 disabled)" -ForegroundColor Gray

    # Test a subset of examples with internal features disabled via command-line flags
    $FallbackExamples = @("descriptorsets", "texture")  # Examples that exercise upload/submit paths
    $FallbackConfigs = @(
        @{ Name = "no-sync2"; Flags = @("--debug-no-sync2") }
    )

    $FallbackTotal = $FallbackExamples.Count * $FallbackConfigs.Count
    $FallbackNum = 0
    $TotalTests += $FallbackTotal

    foreach ($ExampleName in $FallbackExamples) {
        $ExePath = Find-ExampleExe -ExampleName $ExampleName -Config $CoreConfig -AllowFallback $true
        if ($null -eq $ExePath -or -not (Test-Path $ExePath)) {
            Write-Host "  Skipping $ExampleName (not found)" -ForegroundColor Yellow
            continue
        }

        foreach ($config in $FallbackConfigs) {
            $FallbackNum++
            $TestNum++
            $TestName = "$ExampleName [fallback: $($config.Name)]"
            $SafeTestName = "${ExampleName}_fallback_$($config.Name)"
            $LogFile = "$CurrentLogDir\$SafeTestName.log"

            Write-Host "[$FallbackNum/$FallbackTotal] Testing: $ExampleName" -ForegroundColor Yellow
            Write-Host "  Fallback: $($config.Name)" -ForegroundColor Gray

            $AllArgs = @("--validation", "--duration=$Duration") + $config.Flags
            $TimeoutMs = [int](($Duration + 5) * 1000)

            try {
                $result = Invoke-TestRun -ExePath $ExePath -TestArgs $AllArgs -TimeoutMs $TimeoutMs -TestName $TestName -LogFile $LogFile
                $exited = $result.Exited
                $output = $result.Output

                $hasValidationError = Test-ValidationError -Output $output
                $validationErrors = @()
                if ($hasValidationError) {
                    $validationErrors = Get-ValidationErrorSummary -Output $output
                }

                if (-not $exited) {
                    Write-Host "  TIMEOUT" -ForegroundColor Red
                    $FailedTests++
                    $FailedList += $TestName
                } elseif ($hasValidationError) {
                    Write-Host "  VALIDATION ERROR" -ForegroundColor Red
                    $FailedTests++
                    $FailedList += $TestName
                    $ValidationErrorTests += @{ Name = $TestName; Errors = $validationErrors }
                } else {
                    Write-Host "  PASSED" -ForegroundColor Green
                    $PassedTests++
                }
            } catch {
                Write-Host "  FAILED (exception: $_)" -ForegroundColor Red
                $FailedTests++
                $FailedList += $TestName
            }
        }
    }
}

if (-not $SkipPerf) {
    Write-Host ""
    Write-Host "========================================" -ForegroundColor Cyan
    Write-Host "Performance Logging" -ForegroundColor Cyan
    Write-Host "========================================" -ForegroundColor Cyan

    foreach ($ExampleName in $PerfExamples) {
        $ExePath = Find-ExampleExe -ExampleName $ExampleName -Config $PerfConfig -AllowFallback $false
        if ($null -eq $ExePath -or -not (Test-Path $ExePath)) {
            if (-not $SkipBuild) {
                Ensure-Build -Config $PerfConfig | Out-Null
                $ExePath = Find-ExampleExe -ExampleName $ExampleName -Config $PerfConfig -AllowFallback $false
            }
        }
        if ($null -eq $ExePath -or -not (Test-Path $ExePath)) {
            Write-Host "Skipping perf example (missing): $ExampleName (config: $PerfConfig)" -ForegroundColor Yellow
            continue
        }

        $PerfName = "$ExampleName [default flags]"
        $PerfLog = "$CurrentLogDir\perf_$ExampleName.log"
        $TempStdout = "$CurrentLogDir\perf_${ExampleName}_stdout.tmp"
        $TempStderr = "$CurrentLogDir\perf_${ExampleName}_stderr.tmp"
        $PerfArgs = @("--duration=$PerfDuration")
        $TimeoutMs = [int](($PerfDuration + 5) * 1000)

        Write-Host "Perf: $ExampleName" -ForegroundColor Yellow

        # Use synchronous file redirection for reliable capture
        $workDir = Split-Path $ExePath -Parent
        $argString = $PerfArgs -join " "
        $process = Start-Process -FilePath $ExePath -ArgumentList $argString -WorkingDirectory $workDir `
            -RedirectStandardOutput $TempStdout -RedirectStandardError $TempStderr `
            -PassThru -NoNewWindow

        $exited = $process.WaitForExit($TimeoutMs)
        if (-not $exited) {
            try { $process.Kill() } catch {}
        }

        $stdout = if (Test-Path $TempStdout) { Get-Content $TempStdout -Raw } else { "" }
        $stderr = if (Test-Path $TempStderr) { Get-Content $TempStderr -Raw } else { "" }
        $output = "$stdout`n$stderr"

        # Write log file
        $logContent = @"
Test: $PerfName
Executable: $ExePath
Args: $argString
Timestamp: $(Get-Date -Format "yyyy-MM-dd HH:mm:ss")
Exit Code: $($process.ExitCode)
Exited: $exited
----------------------------------------
STDOUT:
$stdout
----------------------------------------
STDERR:
$stderr
"@
        Set-Content -Path $PerfLog -Value $logContent

        # Cleanup temp files
        Remove-Item -Path $TempStdout -ErrorAction SilentlyContinue
        Remove-Item -Path $TempStderr -ErrorAction SilentlyContinue

        $fpsStats = Get-FpsStats -Output $output
        if ($fpsStats -ne $null) {
            $PerfResults += @{
                Example = $ExampleName
                Average = $fpsStats.Average
                Minimum = $fpsStats.Minimum
                Maximum = $fpsStats.Maximum
                Low1Pct = $fpsStats.Low1Pct
                Low01Pct = $fpsStats.Low01Pct
                AvgMs = $fpsStats.AvgMs
                LogFile = $PerfLog
            }
            # Display comprehensive metrics
            $fpsLine = "  FPS: avg={0} min={1} max={2}" -f $fpsStats.Average, $fpsStats.Minimum, $fpsStats.Maximum
            if ($fpsStats.Low1Pct) { $fpsLine += " 1%low={0}" -f $fpsStats.Low1Pct }
            if ($fpsStats.AvgMs) { $fpsLine += " | {0}ms" -f $fpsStats.AvgMs }
            Write-Host $fpsLine -ForegroundColor Gray
        } else {
            $PerfResults += @{
                Example = $ExampleName
                Average = $null
                Minimum = $null
                Maximum = $null
                Low1Pct = $null
                Low01Pct = $null
                AvgMs = $null
                LogFile = $PerfLog
            }
            Write-Host "  FPS not detected (see log)" -ForegroundColor Yellow
        }
    }
}

Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "Test Results" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "Passed: $PassedTests / $TotalTests" -ForegroundColor $(if ($PassedTests -eq $TotalTests) { "Green" } else { "Yellow" })
Write-Host "Failed: $FailedTests / $TotalTests" -ForegroundColor $(if ($FailedTests -eq 0) { "Green" } else { "Red" })
Write-Host "Log directory: $CurrentLogDir" -ForegroundColor Gray

if ($FailedList.Count -gt 0) {
    Write-Host ""
    Write-Host "Failed tests:" -ForegroundColor Red
    foreach ($Test in $FailedList) {
        Write-Host "  - $Test" -ForegroundColor Red
    }

    # Write summary file
    $summaryFile = "$CurrentLogDir\summary.txt"
    $summaryContent = @"
HinaVK Stress Test Summary
Generated: $(Get-Date -Format "yyyy-MM-dd HH:mm:ss")
========================================
Passed: $PassedTests / $TotalTests
Failed: $FailedTests / $TotalTests

Failed Tests:
$($FailedList | ForEach-Object { "  - $_" } | Out-String)
"@
    if ($PerfResults.Count -gt 0) {
        $summaryContent += "`nPerformance (default flags):`n"
        foreach ($p in $PerfResults) {
            if ($p.Average -ne $null) {
                $line = "  $($p.Example): avg=$($p.Average) min=$($p.Minimum) max=$($p.Maximum)"
                if ($p.Low1Pct) { $line += " 1%low=$($p.Low1Pct)" }
                if ($p.Low01Pct) { $line += " 0.1%low=$($p.Low01Pct)" }
                if ($p.AvgMs) { $line += " ($($p.AvgMs)ms)" }
                $summaryContent += "$line`n"
            } else {
                $summaryContent += "  $($p.Example): FPS not detected (see $($p.LogFile))`n"
            }
        }
    }
    if ($ValidationErrorTests.Count -gt 0) {
        $summaryContent += "`nValidation Errors:`n"
        foreach ($vtest in $ValidationErrorTests) {
            $summaryContent += "`n$($vtest.Name):`n"
            foreach ($err in $vtest.Errors) {
                $summaryContent += "  $err`n"
            }
        }
    }
    Set-Content -Path $summaryFile -Value $summaryContent
    Write-Host ""
    Write-Host "Summary written to: $summaryFile" -ForegroundColor Gray

    exit 1
}

Write-Host ""
if ($PerfResults.Count -gt 0) {
    Write-Host "Performance (default flags):" -ForegroundColor Cyan
    foreach ($p in $PerfResults) {
        if ($p.Average -ne $null) {
            $line = "  {0}: avg={1} min={2} max={3}" -f $p.Example, $p.Average, $p.Minimum, $p.Maximum
            if ($p.Low1Pct) { $line += " 1%low={0}" -f $p.Low1Pct }
            if ($p.AvgMs) { $line += " | {0}ms" -f $p.AvgMs }
            Write-Host $line -ForegroundColor Gray
        } else {
            Write-Host ("  {0}: FPS not detected (see {1})" -f $p.Example, $p.LogFile) -ForegroundColor Yellow
        }
    }
}
Write-Host ""
Write-Host "All tests passed!" -ForegroundColor Green
exit 0
