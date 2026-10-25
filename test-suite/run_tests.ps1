param(
    [String]$x,
    [Parameter(Mandatory = $false, Position = 0, ValueFromRemainingArguments = $true)]
    [String[]] $tests
)
# Making Windows PowerShell console window Unicode (UTF-8) aware.
$OutputEncoding = [console]::InputEncoding = [console]::OutputEncoding = New-Object System.Text.UTF8Encoding

$TEST_NAMES = $tests

if ($DNCDBG.count -eq 0) {
    $DNCDBG = "../bin/dncdbg.exe"
}

# Discover test projects: every top-level directory starting with "Test".
# If test names are passed on the command line, run only those instead.
if ($TEST_NAMES.count -eq 0) {
    $TEST_NAMES = Get-ChildItem -Directory -Filter "Test*" | Select-Object -ExpandProperty Name
}

$isWindowsOs = ($env:OS -eq "Windows_NT")

# A test is Windows-only if its project targets a Windows-specific framework
# (e.g. net10.0-windows) or uses Windows Forms; such tests are skipped
# on non-Windows platforms.
function Test-WindowsOnly([String]$testName) {
    $csproj = Get-ChildItem -Path $testName -Filter "*.csproj" -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($null -eq $csproj) {
        return $false
    }
    $content = Get-Content $csproj.FullName -Raw
    return ($content -match "(-windows|UseWindowsForms)")
}

# Prepare
dotnet build Runner
if ($lastexitcode -ne 0) {
    throw ("Exec build test: " + $errorMessage)
}

$test_pass = 0
$test_fail = 0
$test_skip = 0
$test_list = ""

# Build, push and run tests
foreach ($TEST_NAME in $TEST_NAMES) {

    $isWindowsOnly = Test-WindowsOnly $TEST_NAME
    if ($isWindowsOnly -and (-not $isWindowsOs)) {
        $test_skip++
        $test_list = "$test_list$TEST_NAME ... skipped (Windows-only)`n"
        continue
    }

    $BUILD_TYPE = "Debug"
    if ($TEST_NAME.EndsWith("_Release")) {
        $BUILD_TYPE = "Release"
    }

    dotnet build -c $BUILD_TYPE $TEST_NAME
    if ($lastexitcode -ne 0) {
        $test_fail++
        $test_list = "$test_list$TEST_NAME ... failed: build error`n"
        continue
    }

    $SOURCE_FILE_LIST = (Get-ChildItem -Path "$TEST_NAME" -Recurse -Filter *.cs | Where {$_.FullName -notlike "*\obj\*"} | Resolve-path -relative).Substring(2)

    $SOURCE_FILES = ""
    foreach ($SOURCE_FILE in $SOURCE_FILE_LIST) {
        $SOURCE_FILES += $SOURCE_FILE + ";"
    }

    $FRAMEWORK = "net10.0"
    if (-not [String]::IsNullOrEmpty($DNCDBG_TARGET_FRAMEWORK)) {
        $FRAMEWORK = $DNCDBG_TARGET_FRAMEWORK
    }
    if ($isWindowsOnly) {
        $FRAMEWORK += "-windows"
    }

    dotnet run --project Runner -- `
        --local $DNCDBG `
        --test $TEST_NAME `
        --sources $SOURCE_FILES `
        --assembly $TEST_NAME/bin/$BUILD_TYPE/$FRAMEWORK/$TEST_NAME.dll

    if ($lastexitcode -eq 0) {
        $test_pass++
        $test_list = "$test_list$TEST_NAME ... passed`n"
    }
    else {
        $test_fail++
        $test_list = "$test_list$TEST_NAME ... failed`n"
    }
}

Write-Output ""
Write-Output $test_list
Write-Output "Total tests: $($test_pass + $test_fail + $test_skip). Passed: $test_pass. Failed: $test_fail. Skipped: $test_skip."

if ($test_fail -ne 0) {
    exit 1
}
