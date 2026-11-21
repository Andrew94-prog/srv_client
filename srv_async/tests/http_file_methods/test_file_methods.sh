# Functional tests of file serving for all http methods:
# PUT/POST/DELETE storing and removal of files in /storage/,
# GET/HEAD reading, routing and error status codes

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
. "$SCRIPT_DIR/common.sh"

SUITE_NAME="file methods"

# Fixtures: writable area and directory for 409 checks
mkdir -p "$BASE_DIR/storage/subdir"

# Reading of pre-existing files
check "GET /" 200 "$(req "$BASE_URL/")"
check "GET existing file" 200 "$(req "$BASE_URL/files/hello.txt")"
check "GET missing file" 404 "$(req "$BASE_URL/files/missing.txt")"

# PUT: create new file and replace it
check "PUT create" 201 "$(printf 'v1' | \
        req -X PUT --data-binary @- "$BASE_URL/storage/t1.txt")"
check "PUT replace" 204 "$(printf 'v2-longer' | \
        req -X PUT --data-binary @- "$BASE_URL/storage/t1.txt")"
check "PUT stored content" "v2-longer" \
        "$(cat "$BASE_DIR/storage/t1.txt" 2>/dev/null)"

loc=$(printf 'x' | curl -s -i -X PUT --data-binary @- \
        -H "Host: $HOST_NAME" "$BASE_URL/storage/loc.txt" | \
        tr -d '\r' | grep -i '^location:' | awk '{print $2}')
check "PUT 201 Location header" "/storage/loc.txt" "$loc"

line=$(printf 'x' | curl -s -i -X PUT --data-binary @- \
        -H "Host: $HOST_NAME" "$BASE_URL/storage/sl.txt" | \
        head -1 | tr -d '\r')
check "201 status line with reason phrase" \
        "HTTP/1.1 201 Created" "$line"

# POST: create new file and replace it, response reports the action
check "POST create" 200 "$(printf 'p1' | \
        req -X POST --data-binary @- "$BASE_URL/storage/t2.txt")"
body=$(printf 'p2' | curl -s -X POST --data-binary @- \
        -H "Host: $HOST_NAME" "$BASE_URL/storage/t2.txt")
case "$body" in
    *"replaced"*) post_replace="yes" ;;
    *)            post_replace="no" ;;
esac
check "POST replace reports replacement" "yes" "$post_replace"

# GET/HEAD of stored file
get_body=$(curl -s -H "Host: $HOST_NAME" "$BASE_URL/storage/t1.txt")
case "$get_body" in
    *"v2-longer"*) get_content="yes" ;;
    *)             get_content="no" ;;
esac
check "GET returns stored content" "yes" "$get_content"
check "HEAD stored file" 200 "$(req -I "$BASE_URL/storage/t1.txt")"

# DELETE: remove existing file, second attempt finds nothing
check "DELETE existing" 204 "$(req -X DELETE "$BASE_URL/storage/t1.txt")"
check "DELETE again" 404 "$(req -X DELETE "$BASE_URL/storage/t1.txt")"
check "GET deleted file" 404 "$(req "$BASE_URL/storage/t1.txt")"

# Error paths inside /storage/
check "PUT to directory" 409 "$(printf 'x' | \
        req -X PUT --data-binary @- "$BASE_URL/storage/subdir")"
check "PUT without parent directory" 409 "$(printf 'x' | \
        req -X PUT --data-binary @- "$BASE_URL/storage/nodir/f.txt")"
check "DELETE directory" 404 "$(req -X DELETE "$BASE_URL/storage/subdir")"

# Routing: api is not implemented, foreign hosts are not served
check "api GET" 501 "$(req "$BASE_URL/api/test")"
check "api PUT" 501 "$(printf 'x' | \
        req -X PUT --data-binary @- "$BASE_URL/api/test")"
check "api DELETE" 501 "$(req -X DELETE "$BASE_URL/api/test")"
line=$(curl -s -i -H "Host: $HOST_NAME" "$BASE_URL/api/test" | \
        head -1 | tr -d '\r')
check "501 status line with reason phrase" \
        "HTTP/1.1 501 Not Implemented" "$line"
check "foreign host" 403 "$(curl -s -o /dev/null -w '%{http_code}' \
        -m 30 -X DELETE -H "Host: other.com" \
        "$BASE_URL/storage/x.txt")"
line=$(curl -s -i -X DELETE -H "Host: other.com" \
        "$BASE_URL/storage/x.txt" | head -1 | tr -d '\r')
check "403 status line with reason phrase" \
        "HTTP/1.1 403 Forbidden" "$line"

# Two requests over one keep-alive connection (curl writes out
# once per transfer, so two successful statuses concatenate)
ka=$(curl -s -o /dev/null -o /dev/null -w '%{http_code}' -m 30 \
        -H "Host: $HOST_NAME" "$BASE_URL/files/hello.txt" \
        "$BASE_URL/files/hello.txt")
check "keep-alive, two requests on one connection" "200200" "$ka"

rm -f "$BASE_DIR/storage/t1.txt" "$BASE_DIR/storage/t2.txt" \
      "$BASE_DIR/storage/loc.txt" "$BASE_DIR/storage/sl.txt"

finish
