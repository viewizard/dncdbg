param(
    [String]$x,
    [Parameter(Mandatory = $false, Position = 0, ValueFromRemainingArguments = $true)]
    [String[]] $tests
)
# Making Windows PowerShell console window Unicode (UTF-8) aware.
$OutputEncoding = [console]::InputEncoding = [console]::OutputEncoding = New-Object System.Text.UTF8Encoding

$ALL_TEST_NAMES = @(
    "TestBreakpoint"
    "TestFuncBreak"
    "TestAttach"
    "TestPause"
    "TestDisconnect"
    "TestThreads"
    "TestVariables"
    "TestEvaluate"
    "TestStepping"
    "TestEnv"
    "TestExitCode"
    "TestEvalNotEnglish"
    "Test中文目录"
    "TestSrcBreakpointResolve"
    "TestEnum"
    "TestAsyncStepping"
    "TestBreak"
    "TestNoJMCNoFilterStepping"
    "TestNoJMCBreakpoint"
    "TestNoJMCAsyncStepping"
    "TestExceptionBreakpoint"
    "TestNoJMCExceptionBreakpoint"
    "TestSizeof"
    "TestAsyncLambdaEvaluate"
    "TestGeneric"
    "TestEvalArraysIndexers"
    "TestExtensionMethods"
    "TestBreakpointWithoutStop"
    "TestUnhandledException"
)

$TEST_NAMES = $tests

if ($DNCDBG.count -eq 0) {
    $DNCDBG = "../bin/dncdbg.exe"
}

if ($TEST_NAMES.count -eq 0) {
    $TEST_NAMES = $ALL_TEST_NAMES
}

# Prepare
dotnet build Runner
if ($lastexitcode -ne 0) {
    throw ("Exec build test: " + $errorMessage)
}

$test_pass = 0
$test_fail = 0
$test_list = ""

# Build, push and run tests
foreach ($TEST_NAME in $TEST_NAMES) {
    dotnet build $TEST_NAME
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

    dotnet run --project Runner -- `
        --local $DNCDBG `
        --test $TEST_NAME `
        --sources $SOURCE_FILES `
        --assembly $TEST_NAME/bin/Debug/net10.0/$TEST_NAME.dll

    if($?)
    {
        $test_pass++
        $test_list = "$test_list$TEST_NAME ... passed`n"
    }
    else
    {
        $test_fail++
        $test_list = "$test_list$TEST_NAME ... failed`n"
    }
}

Write-Host ""
Write-Host $test_list
Write-Host "Total tests: $($test_pass + $test_fail). Passed: $test_pass. Failed: $test_fail."
