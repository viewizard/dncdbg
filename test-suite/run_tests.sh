#!/bin/bash

ALL_TEST_NAMES=(
    "VSCodeTestBreakpoint"
    "VSCodeTestFuncBreak"
    "VSCodeTestAttach"
    "VSCodeTestPause"
    "VSCodeTestDisconnect"
    "VSCodeTestThreads"
    "VSCodeTestVariables"
    "VSCodeTestEvaluate"
    "VSCodeTestStepping"
    "VSCodeTestEnv"
    "VSCodeTestExitCode"
    "VSCodeTestEvalNotEnglish"
    "VSCodeTest中文目录"
    "VSCodeTestSrcBreakpointResolve"
    "VSCodeTestEnum"
    "VSCodeTestAsyncStepping"
    "VSCodeTestBreak"
    "VSCodeTestNoJMCNoFilterStepping"
    "VSCodeTestNoJMCBreakpoint"
    "VSCodeTestNoJMCAsyncStepping"
    "VSCodeTestExceptionBreakpoint"
    "VSCodeTestNoJMCExceptionBreakpoint"
    "VSCodeTestSizeof"
    "VSCodeTestAsyncLambdaEvaluate"
    "VSCodeTestGeneric"
    "VSCodeTestEvalArraysIndexers"
    "VSCodeTestExtensionMethods"
    "VSCodeTestBreakpointWithoutStop"
    "VSCodeTestUnhandledException"
)

TEST_NAMES="$@"

if [[ -z $NETCOREDBG ]]; then
    NETCOREDBG="../bin/netcoredbg"
fi

if [[ -z $TEST_NAMES ]]; then
    TEST_NAMES="${ALL_TEST_NAMES[@]}"
fi

dotnet build TestRunner || exit $?

test_pass=0
test_fail=0
test_count=0
test_list=""

for TEST_NAME in $TEST_NAMES; do
    dotnet build $TEST_NAME || {
        echo "$TEST_NAME: build error." >&2
        test_fail=$(($test_fail + 1))
        test_list="$test_list$TEST_NAME ... failed: build error\n"
        continue
    }

    SOURCE_FILES=""
    for file in `find $TEST_NAME \! -path "$TEST_NAME/obj/*" -type f -name "*.cs"`; do
        SOURCE_FILES="${SOURCE_FILES}${file};"
    done

    dotnet run --project TestRunner -- \
        --local $NETCOREDBG \
        --test $TEST_NAME \
        --sources "$SOURCE_FILES" \
        --assembly $TEST_NAME/bin/Debug/net10.0/$TEST_NAME.dll

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
