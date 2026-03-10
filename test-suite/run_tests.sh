#!/bin/bash

ALL_TEST_NAMES=(
    "TestBreakpoint"
    "TestBreakpoint_Release"
    "TestBreakpoint_NoJMC"
    "TestBreakpoint_NoJMC_Release"
    "TestFuncBreak"
    "TestAttach"
    "TestPause"
    "TestDisconnect"
    "TestThreads"
    "TestVariables"
    "TestEvaluate"
    "TestStepping"
    "TestStepping_NoJMCNoFilter"
    "TestEnv"
    "TestExitCode"
    "TestEvalNotEnglish"
    "Test中文目录"
    "TestSrcBreakpointResolve"
    "TestEnum"
    "TestAsyncStepping"
    "TestAsyncStepping_NoJMC"
    "TestBreak"
    "TestExceptionBreakpoint"
    "TestExceptionBreakpoint_NoJMC"
    "TestSizeof"
    "TestAsyncLambdaEvaluate"
    "TestGeneric"
    "TestEvalArraysIndexers"
    "TestExtensionMethods"
    "TestBreakpointWithoutStop"
    "TestUnhandledException"
    "TestStdIO"
)

TEST_NAMES="$@"

if [[ -z $DNCDBG ]]; then
    DNCDBG="../bin/dncdbg"
fi

if [[ -z $TEST_NAMES ]]; then
    TEST_NAMES="${ALL_TEST_NAMES[@]}"
fi

dotnet build Runner || exit $?

test_pass=0
test_fail=0
test_count=0
test_list=""

for TEST_NAME in $TEST_NAMES; do

    BUILD_TYPE=Debug

    if  [[ $TEST_NAME == *_Release ]] ;
    then
        BUILD_TYPE=Release
    fi

    dotnet build -c $BUILD_TYPE $TEST_NAME || {
        echo "$TEST_NAME: build error." >&2
        test_fail=$(($test_fail + 1))
        test_list="$test_list$TEST_NAME ... failed: build error\n"
        continue
    }

    SOURCE_FILES=""
    for file in `find $TEST_NAME \! -path "$TEST_NAME/obj/*" -type f -name "*.cs"`; do
        SOURCE_FILES="${SOURCE_FILES}${file};"
    done

    dotnet run --project Runner -- \
        --local $DNCDBG \
        --test $TEST_NAME \
        --sources "$SOURCE_FILES" \
        --assembly $TEST_NAME/bin/$BUILD_TYPE/net10.0/$TEST_NAME.dll

    if [ "$?" -ne "0" ]; then
        test_fail=$(($test_fail + 1))
        test_list="$test_list$TEST_NAME ... failed res=$res\n"
    else
        test_pass=$(($test_pass + 1))
        test_list="$test_list$TEST_NAME ... passed\n"
    fi
done

echo ""
echo -e $test_list
echo "Total tests: $(($test_pass + $test_fail)). Passed: $test_pass. Failed: $test_fail."
