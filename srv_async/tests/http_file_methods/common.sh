# Common helpers for http test suites. Sourced by test_*.sh scripts:
# defaults for server location, curl wrapper and check counters

TESTS_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)

# Without run_tests.sh the suites work against the development
# server started manually in the project directory
: "${BASE_URL:=http://127.0.0.1:8080}"
: "${HOST_NAME:=example.com}"
: "${BASE_DIR:=$(cd "$TESTS_DIR/../.." && pwd)/public}"

PASS=0
FAIL=0

# One request to the tested server, prints http status code.
# Additional curl arguments are passed as is
req()
{
    curl -s -o /dev/null -w '%{http_code}' -m 30 \
            -H "Host: $HOST_NAME" "$@"
}

# Report result of one check and update counters
check()
{
    local desc="$1" expected="$2" actual="$3"

    if [ "$expected" = "$actual" ]; then
        PASS=$((PASS + 1))
        echo "PASS: $desc"
    else
        FAIL=$((FAIL + 1))
        echo "FAIL: $desc (expected '$expected', got '$actual')"
    fi
}

# Same as check(), but actual value must be not less than expected
check_ge()
{
    local desc="$1" expected="$2" actual="$3"

    if [ "$actual" -ge "$expected" ] 2>/dev/null; then
        PASS=$((PASS + 1))
        echo "PASS: $desc"
    else
        FAIL=$((FAIL + 1))
        echo "FAIL: $desc (expected at least '$expected', got '$actual')"
    fi
}

# Print summary, exit with 0 only if nothing failed
finish()
{
    echo "---- $SUITE_NAME: $PASS passed, $FAIL failed"
    [ "$FAIL" -eq 0 ]
}
