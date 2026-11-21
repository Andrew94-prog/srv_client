# Test runner: builds the server if needed, starts its own instance
# with separate config, port and base directory (so a development
# server in the project directory is not disturbed), runs the test
# suites against it and stops the server.
#
# Usage: run_tests.sh [suite script ...]   (without arguments: all)

set -u

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
PROJ_DIR=$(cd "$SCRIPT_DIR/../.." && pwd)

TEST_PORT=18080
NUM_WORKERS=4
BASE_URL="http://127.0.0.1:$TEST_PORT"
RUN_DIR=""
FAILED=0

cleanup()
{
    if [ -n "$RUN_DIR" ] && [ -d "$RUN_DIR" ]; then
        if [ -f "$RUN_DIR/srv_pid" ]; then
            SRV_PID=$(cat "$RUN_DIR/srv_pid")
            pkill -TERM -P "$SRV_PID" 2>/dev/null
            kill "$SRV_PID" 2>/dev/null
        fi
        if [ "$FAILED" -eq 0 ]; then
            rm -rf "$RUN_DIR"
        else
            echo "server log and base dir are kept in $RUN_DIR"
        fi
    fi
}
trap cleanup EXIT

fail()
{
    echo "ERROR: $*"
    [ -f "$RUN_DIR/test_srv.log" ] && tail -20 "$RUN_DIR/test_srv.log"
    exit 1
}

# Server binary is built on demand
if [ ! -x "$PROJ_DIR/srv_async" ]; then
    echo "building server..."
    make -C "$PROJ_DIR" >/dev/null || fail "build of server failed"
fi

# Test port must be free
if curl -s -o /dev/null -m 2 "$BASE_URL/"; then
    fail "port $TEST_PORT is already in use"
fi

# Host name is taken from the project config
HOST_NAME=$(sed -n 's/^host_name:[[:space:]]*//p' \
        "$PROJ_DIR/srv.config" 2>/dev/null | head -1)
: "${HOST_NAME:=example.com}"

# Run directory with own base dir and fixture files
RUN_DIR=$(mktemp -d /tmp/srv_async_tests.XXXXXX) || fail "mktemp failed"
mkdir -p "$RUN_DIR/public/files" \
         "$RUN_DIR/public/static" \
         "$RUN_DIR/public/storage"
echo '<html><body>test index</body></html>' \
        > "$RUN_DIR/public/index.html"
echo 'hello from downloadable file' > "$RUN_DIR/public/files/hello.txt"
echo 'body {}' > "$RUN_DIR/public/static/style.css"

# Config: same as the project one, but with own port, log and base dir
sed -e "s/^port:.*/port: $TEST_PORT/" \
    -e "s/^num_workers:.*/num_workers: $NUM_WORKERS/" \
    -e "s|^log_file:.*|log_file: $RUN_DIR/test_srv.log|" \
    -e "s|^base_dir:.*|base_dir: $RUN_DIR/public|" \
    "$PROJ_DIR/srv.config" > "$RUN_DIR/srv.config" || \
    fail "project srv.config not found"

( cd "$RUN_DIR" && exec "$PROJ_DIR/srv_async" ) >/dev/null 2>&1 &
echo $! > "$RUN_DIR/srv_pid"

ready=0
for i in $(seq 1 50); do
    code=$(curl -s -o /dev/null -w '%{http_code}' -m 2 \
            -H "Host: $HOST_NAME" "$BASE_URL/" 2>/dev/null)
    if [ "$code" = "200" ]; then
        ready=1
        break
    fi
    sleep 0.2
done
[ "$ready" = 1 ] || fail "server did not start on port $TEST_PORT"

SRV_PID=$(cat "$RUN_DIR/srv_pid")
echo "server started: pid $SRV_PID, $(pgrep -P "$SRV_PID" | wc -l)"\
        "workers, base dir $RUN_DIR/public"

export BASE_URL HOST_NAME SRV_PID NUM_WORKERS
export BASE_DIR="$RUN_DIR/public"

SUITES="${*:-test_file_methods.sh test_storage_restrict.sh \
test_large_bodies.sh}"

for suite in $SUITES; do
    echo
    echo "======== $suite ========"
    bash "$SCRIPT_DIR/$suite" || FAILED=1
done

echo
if [ "$FAILED" -eq 0 ]; then
    echo "all test suites passed"
else
    echo "some tests FAILED"
fi
exit "$FAILED"
