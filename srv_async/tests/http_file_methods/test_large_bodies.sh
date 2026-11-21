# Regression tests for receiving and sending of large request
# bodies (the fixed deadlock of coroutines parked on EAGAIN):
# multi-chunk uploads, multi-megabyte transfers in both
# directions and concurrent connections

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
. "$SCRIPT_DIR/common.sh"

SUITE_NAME="large bodies"

TMP=$(mktemp -d /tmp/srv_async_large.XXXXXX)
trap 'rm -rf "$TMP"' EXIT

ONE_MB=1048576
SIX_MB=6291456
THREE_MB=3145728

# Repeated 1 MiB upload: create, then two replacements
head -c "$ONE_MB" /dev/urandom > "$TMP/f1m.bin"
check "1 MiB PUT create" 201 "$(curl -s -o /dev/null -w '%{http_code}' \
        -m 60 -X PUT --data-binary "@$TMP/f1m.bin" -H "Host: $HOST_NAME" \
        "$BASE_URL/storage/f1m.bin")"
check "1 MiB PUT replace" 204 "$(curl -s -o /dev/null -w '%{http_code}' \
        -m 60 -X PUT --data-binary "@$TMP/f1m.bin" -H "Host: $HOST_NAME" \
        "$BASE_URL/storage/f1m.bin")"
check "1 MiB PUT replace again" 204 \
        "$(curl -s -o /dev/null -w '%{http_code}' -m 60 -X PUT \
        --data-binary "@$TMP/f1m.bin" -H "Host: $HOST_NAME" \
        "$BASE_URL/storage/f1m.bin")"
md5_a=$(md5sum "$TMP/f1m.bin" | awk '{print $1}')
md5_b=$(md5sum "$BASE_DIR/storage/f1m.bin" 2>/dev/null | \
        awk '{print $1}')
check "1 MiB stored content" "$md5_a" "$md5_b"

# 6 MiB PUT (this size hanged the server before the SIGIO fix)
head -c "$SIX_MB" /dev/urandom > "$TMP/f6m.bin"
check "6 MiB PUT" 201 "$(curl -s -o /dev/null -w '%{http_code}' \
        -m 60 -X PUT --data-binary "@$TMP/f6m.bin" -H "Host: $HOST_NAME" \
        "$BASE_URL/storage/f6m.bin")"
check "6 MiB stored size" "$SIX_MB" \
        "$(stat -c%s "$BASE_DIR/storage/f6m.bin" 2>/dev/null)"
md5_a=$(md5sum "$TMP/f6m.bin" | awk '{print $1}')
md5_b=$(md5sum "$BASE_DIR/storage/f6m.bin" 2>/dev/null | \
        awk '{print $1}')
check "6 MiB stored content" "$md5_a" "$md5_b"

check "6 MiB POST" 200 "$(curl -s -o /dev/null -w '%{http_code}' \
        -m 60 -X POST --data-binary "@$TMP/f6m.bin" \
        -H "Host: $HOST_NAME" "$BASE_URL/storage/f6m_post.bin")"
md5_b=$(md5sum "$BASE_DIR/storage/f6m_post.bin" 2>/dev/null | \
        awk '{print $1}')
check "6 MiB POST stored content" "$md5_a" "$md5_b"

# Download of the large file: full body must arrive
out=$(curl -s -o "$TMP/f6m_dl.bin" -w '%{http_code} %{size_download}' \
        -m 60 -H "Host: $HOST_NAME" "$BASE_URL/storage/f6m.bin")
check "6 MiB GET status" 200 "${out%% *}"
check_ge "6 MiB GET downloaded size" "$SIX_MB" "${out##* }"

# Four concurrent uploads and downloads on separate connections
for i in 1 2 3 4; do
    (
        head -c "$THREE_MB" /dev/urandom > "$TMP/c$i.bin"
        curl -s -o /dev/null -w '%{http_code}' -m 60 -X PUT \
                --data-binary "@$TMP/c$i.bin" -H "Host: $HOST_NAME" \
                "$BASE_URL/storage/c$i.bin" > "$TMP/c$i.upcode"
        curl -s -o /dev/null -w '%{http_code}' -m 60 \
                -H "Host: $HOST_NAME" \
                "$BASE_URL/storage/c$i.bin" > "$TMP/c$i.dlcode"
    ) &
done
wait
for i in 1 2 3 4; do
    check "concurrent PUT $i" 201 "$(cat "$TMP/c$i.upcode" 2>/dev/null)"
    md5_a=$(md5sum "$TMP/c$i.bin" | awk '{print $1}')
    md5_b=$(md5sum "$BASE_DIR/storage/c$i.bin" 2>/dev/null | \
            awk '{print $1}')
    check "concurrent PUT $i content" "$md5_a" "$md5_b"
    check "concurrent GET $i" 200 "$(cat "$TMP/c$i.dlcode" 2>/dev/null)"
done

# All worker processes must survive the transfers (checked when
# the suite is run by run_tests.sh with its own server instance)
if [ -n "${SRV_PID:-}" ]; then
    n=$(pgrep -P "$SRV_PID" | wc -l)
    check "workers alive after transfers" "${NUM_WORKERS:-4}" "$n"
fi

for f in f1m f6m f6m_post c1 c2 c3 c4; do
    req -X DELETE "$BASE_URL/storage/$f.bin" >/dev/null
done

finish
