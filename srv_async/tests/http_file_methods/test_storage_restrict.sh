# Tests of the modification policy: POST/PUT/DELETE requests
# must be allowed only inside /storage/, everything else
# (including /, /static and /files) is read-only

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
. "$SCRIPT_DIR/common.sh"

SUITE_NAME="storage restriction"

# Checksums of protected files before modification attempts
idx_md5=$(md5sum "$BASE_DIR/index.html" 2>/dev/null | awk '{print $1}')
hello_md5=$(md5sum "$BASE_DIR/files/hello.txt" 2>/dev/null | \
        awk '{print $1}')
css_md5=$(md5sum "$BASE_DIR/static/style.css" 2>/dev/null | \
        awk '{print $1}')

# Modifications inside /storage/ are allowed
check "PUT inside /storage/" 201 "$(printf 'v1' | \
        req -X PUT --data-binary @- "$BASE_URL/storage/rw.txt")"
check "PUT replace inside /storage/" 204 "$(printf 'v2' | \
        req -X PUT --data-binary @- "$BASE_URL/storage/rw.txt")"
check "POST inside /storage/" 200 "$(printf 'p' | \
        req -X POST --data-binary @- "$BASE_URL/storage/rw2.txt")"
check "GET inside /storage/" 200 "$(req "$BASE_URL/storage/rw.txt")"
check "DELETE inside /storage/" 204 \
        "$(req -X DELETE "$BASE_URL/storage/rw.txt")"
check "DELETE missing inside /storage/" 404 \
        "$(req -X DELETE "$BASE_URL/storage/missing.txt")"

# Modifications outside /storage/ are forbidden
for target in / /index.html /static/x.css /files/hello.txt /storage; do
    check "PUT $target" 403 "$(printf 'hacked' | \
            req -X PUT --data-binary @- "$BASE_URL$target")"
    check "POST $target" 403 "$(printf 'hacked' | \
            req -X POST --data-binary @- "$BASE_URL$target")"
    check "DELETE $target" 403 "$(req -X DELETE "$BASE_URL$target")"
done
check "DELETE /static directory" 403 \
        "$(req -X DELETE "$BASE_URL/static")"

# Protected files must stay unchanged
check "index.html unchanged" "$idx_md5" \
        "$(md5sum "$BASE_DIR/index.html" 2>/dev/null | awk '{print $1}')"
check "files/hello.txt unchanged" "$hello_md5" \
        "$(md5sum "$BASE_DIR/files/hello.txt" 2>/dev/null | \
        awk '{print $1}')"
check "static/style.css unchanged" "$css_md5" \
        "$(md5sum "$BASE_DIR/static/style.css" 2>/dev/null | \
        awk '{print $1}')"
check "GET / still works" 200 "$(req "$BASE_URL/")"

rm -f "$BASE_DIR/storage/rw.txt" "$BASE_DIR/storage/rw2.txt"

finish
