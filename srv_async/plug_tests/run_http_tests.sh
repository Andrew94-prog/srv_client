#!/bin/bash
#
# Functional tests for coroutine-based async http server.
#
# The script starts srv_async from the project root (built by "make"),
# sends requests to it by curl/nc and checks responses.
#
# Usage: tests/run_http_tests.sh [port]
#

PORT=${1:-8081}
SRV=./srv_async
LOG=/tmp/srv_async_http_tests.log
SRV_PID=""

# Number of passed and failed tests
PASS=0
FAIL=0

cleanup()
{
    # Kill server and all its forked workers, otherwise workers
    # stay alive and keep listening on the port due to SO_REUSEPORT
    if [ -n "$SRV_PID" ]; then
        kill $SRV_PID $(pgrep -P $SRV_PID) 2>/dev/null
    fi
}
trap cleanup EXIT

# check_desc expected actual
check()
{
    if [ "$2" == "$3" ]; then
        echo "PASS: $1"
        PASS=$((PASS + 1))
    else
        echo "FAIL: $1 (expected \"$2\", got \"$3\")"
        FAIL=$((FAIL + 1))
    fi
}

if [ ! -x $SRV ]; then
    echo "server binary $SRV not found, run make first"
    exit 1
fi

cleanup_all_srv_instances()
{
    pkill -x srv_async 2>/dev/null
    sleep 0.3
}

cleanup_all_srv_instances

# Server is started with single worker (-w 1): with several workers
# from default srv.config the server occasionally does not answer
# on a connection accepted by one of the workers, which makes
# tests flaky
$SRV -p $PORT -w 1 -f $LOG -l 1 &
SRV_PID=$!
sleep 0.5

echo "=== Basic GET and HEAD handling ==="

check "GET / returns 200" "200" \
    "$(curl -s -o /dev/null -w '%{http_code}' http://127.0.0.1:$PORT/)"

check "GET /index.html returns 200" "200" \
    "$(curl -s -o /dev/null -w '%{http_code}' http://127.0.0.1:$PORT/index.html)"

check "GET unknown target returns 404" "404" \
    "$(curl -s -o /dev/null -w '%{http_code}' http://127.0.0.1:$PORT/no_such_page)"

check "GET / returns main page" "Main Page" \
    "$(curl -s http://127.0.0.1:$PORT/ | grep -o '<title>[^<]*</title>' | sed 's/<[^>]*>//g')"

check "HEAD / returns 200" "200" \
    "$(curl -s -o /dev/null -w '%{http_code}' -I http://127.0.0.1:$PORT/)"

check "HEAD / response has no body" "0" \
    "$(curl -s --max-time 6 -X HEAD -o /dev/null -w '%{size_download}' http://127.0.0.1:$PORT/)"

check "HEAD / Content-Length equals size of GET body" \
    "$(curl -s http://127.0.0.1:$PORT/ | wc -c)" \
    "$(curl -s -I http://127.0.0.1:$PORT/ | tr -d '\r' | awk '/^Content-Length:/{print $2}')"

echo "=== Plug responses for not implemented methods ==="

check "POST returns 501" "501" \
    "$(curl -s -o /dev/null -w '%{http_code}' -X POST -d 'abc' http://127.0.0.1:$PORT/)"

check "PUT returns 501" "501" \
    "$(curl -s -o /dev/null -w '%{http_code}' -X PUT -d 'a' http://127.0.0.1:$PORT/item)"

check "DELETE returns 501" "501" \
    "$(curl -s -o /dev/null -w '%{http_code}' -X DELETE http://127.0.0.1:$PORT/item)"

check "PATCH returns 501" "501" \
    "$(curl -s -o /dev/null -w '%{http_code}' -X PATCH -d 'a=b' http://127.0.0.1:$PORT/item)"

echo "=== Response on invalid requests ==="

check "unsupported method returns 405" "HTTP/1.1 405 Method Not Allowed" \
    "$(printf 'OPTIONS / HTTP/1.1\r\nHost: x\r\n\r\n' | timeout 4 nc 127.0.0.1 $PORT | head -1 | tr -d '\r')"

check "garbage request returns 405" "HTTP/1.1 405 Method Not Allowed" \
    "$(printf 'BROKEN\r\n\r\n' | timeout 4 nc 127.0.0.1 $PORT | head -1 | tr -d '\r')"

check "POST without Content-Length returns 411" "HTTP/1.1 411 Length Required" \
    "$(printf 'POST / HTTP/1.1\r\nHost: x\r\n\r\n' | timeout 4 nc 127.0.0.1 $PORT | head -1 | tr -d '\r')"

check "POST with invalid Content-Length returns 400" "HTTP/1.1 400 Bad Request" \
    "$(printf 'POST / HTTP/1.1\r\nHost: x\r\nContent-Length: abc\r\n\r\n' | timeout 4 nc 127.0.0.1 $PORT | head -1 | tr -d '\r')"

check "POST with too large Content-Length returns 413" "HTTP/1.1 413 Payload Too Large" \
    "$(printf 'POST / HTTP/1.1\r\nHost: x\r\nContent-Length: 999999999\r\n\r\n' | timeout 4 nc 127.0.0.1 $PORT | head -1 | tr -d '\r')"

check "headers larger than RECV_BUF_SIZE return 431" "431" \
    "$(curl -s -o /dev/null -w '%{http_code}' -H "Big: $(python3 -c 'print("x"*5000)')" http://127.0.0.1:$PORT/)"

echo "=== Request body receiving ==="

check "POST with 0-length body returns 501" "HTTP/1.1 501 Not Implemented" \
    "$(printf 'POST / HTTP/1.1\r\nHost: x\r\nContent-Length: 0\r\n\r\n' | timeout 4 nc 127.0.0.1 $PORT | head -1 | tr -d '\r')"

check "POST with 1MB body returns 501" "501" \
    "$(curl -s -o /dev/null -w '%{http_code}' -X POST -d @<(python3 -c 'print("x"*1000000)') http://127.0.0.1:$PORT/)"

echo "=== HTTP keep-alive ==="

check "requests 2nd and 3rd of 3 reuse single connection" "2" \
    "$(curl -sv http://127.0.0.1:$PORT/ http://127.0.0.1:$PORT/index.html http://127.0.0.1:$PORT/missing 2>&1 | grep -c 'Re-using existing connection')"

check "connection is not reused after Connection: close" "0" \
    "$(curl -sv -H 'Connection: close' http://127.0.0.1:$PORT/ http://127.0.0.1:$PORT/ 2>&1 | grep -c 'Re-using existing connection')"

check "HTTP/1.0 connection is closed after response" "0" \
    "$(curl -sv --http1.0 http://127.0.0.1:$PORT/ http://127.0.0.1:$PORT/ 2>&1 | grep -c 'Re-using existing connection')"

echo
echo "Tests passed: $PASS, failed: $FAIL"

[ $FAIL -eq 0 ]
