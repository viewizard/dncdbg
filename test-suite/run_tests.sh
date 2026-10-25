#!/bin/bash

TEST_NAMES=("$@")

if [[ -z $DNCDBG ]]; then
    DNCDBG="../bin/dncdbg"
fi

# Discover test projects: every top-level directory starting with "Test".
# If test names are passed on the command line, run only those instead.
if [[ ${#TEST_NAMES[@]} -eq 0 ]]; then
    TEST_NAMES=()
    for dir in Test*/; do
        [[ -d "$dir" ]] && TEST_NAMES+=("${dir%/}")
    done
fi

IS_WINDOWS=false
case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*) IS_WINDOWS=true ;;
esac

# A test is Windows-only if its project targets a Windows-specific framework
# (e.g. net10.0-windows) or uses Windows Forms; such tests are skipped
# on non-Windows platforms.
is_windows_only_test() {
    local csproj=( "$1"/*.csproj )
    [[ -f "${csproj[0]}" ]] && grep -qE '(-windows|UseWindowsForms)' "${csproj[0]}"
}

dotnet build Runner || exit $?

test_pass=0
test_fail=0
test_skip=0
test_list=""

for TEST_NAME in "${TEST_NAMES[@]}"; do

    if is_windows_only_test "$TEST_NAME" && [[ $IS_WINDOWS == false ]]; then
        test_skip=$((test_skip + 1))
        test_list="$test_list$TEST_NAME ... skipped (windows-only)\n"
        continue
    fi

    BUILD_TYPE=Debug

    if  [[ $TEST_NAME == *_Release ]] ;
    then
        BUILD_TYPE=Release
    fi

    dotnet build -c $BUILD_TYPE "$TEST_NAME" || {
        echo "$TEST_NAME: build error." >&2
        test_fail=$(($test_fail + 1))
        test_list="$test_list$TEST_NAME ... failed: build error\n"
        continue
    }

    SOURCE_FILES=""
    while IFS= read -r file; do
        SOURCE_FILES="${SOURCE_FILES}${file};"
    done < <(find "$TEST_NAME" \! -path "$TEST_NAME/obj/*" -type f -name "*.cs")

    FRAMEWORK="net10.0"
    if [[ -n $DNCDBG_TARGET_FRAMEWORK ]]; then
        FRAMEWORK="$DNCDBG_TARGET_FRAMEWORK"
    fi
    if is_windows_only_test "$TEST_NAME"; then
        FRAMEWORK="$FRAMEWORK-windows"
    fi

    dotnet run --project Runner -- \
        --local "$DNCDBG" \
        --test "$TEST_NAME" \
        --sources "$SOURCE_FILES" \
        --assembly "$TEST_NAME/bin/$BUILD_TYPE/$FRAMEWORK/$TEST_NAME.dll"

    res=$?
    if [ "$res" -ne "0" ]; then
        test_fail=$(($test_fail + 1))
        test_list="$test_list$TEST_NAME ... failed res=$res\n"
    else
        test_pass=$(($test_pass + 1))
        test_list="$test_list$TEST_NAME ... passed\n"
    fi
done

echo ""
echo -e "$test_list"
echo "Total tests: $(($test_pass + $test_fail + $test_skip)). Passed: $test_pass. Failed: $test_fail. Skipped: $test_skip."

if [ "$test_fail" -ne 0 ]; then
  exit 1
fi
