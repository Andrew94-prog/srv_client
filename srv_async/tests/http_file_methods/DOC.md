# Documentation of http file method tests (directory `tests/http_file_methods/`)

Black-box tests of the http layer with curl: file serving for all
methods, the modification policy and large bodies. The checks were
used during development of POST/PUT/DELETE file handling and of the
core fixes, and are kept here as regression tests.

Running:

- `make` / `make test` — all suites (via `run_tests.sh`, which starts
  its own server instance)
- `make file_methods`, `make storage_restrict`, `make large_bodies` —
  one suite
- `./test_*.sh` — a single suite by hand against the development
  server on `http://127.0.0.1:8080` (base dir defaults to `../../public`)

Files:

- `run_tests.sh` — runner: builds the server if needed, prepares a
  temporary run directory with its own `srv.config` (port 18080,
  `num_workers` 4), fixture base dir (`index.html`, `files/hello.txt`,
  `static/style.css`, empty `storage/`) and log file, starts/stops the
  server, exports `BASE_URL`, `HOST_NAME`, `BASE_DIR`, `SRV_PID` and
  `NUM_WORKERS` for the suites. On failure keeps the run directory
  and prints the path
- `common.sh` — shared helpers: `req()` (curl wrapper printing the
  status code with the configured `Host` header), `check()` /
  `check_ge()` (expected vs actual comparison with PASS/FAIL report
  and counters), `finish()` (suite summary and exit code). Also holds
  the standalone defaults for `BASE_URL`, `HOST_NAME` and `BASE_DIR`
- `test_file_methods.sh` — functional tests: GET/HEAD reading of
  existing files (200/404), PUT create/replace (201 with `Location`
  header and reason-phrase check / 204) with content verification on
  disk, POST create/replace (200, the body must report created or
  replaced), DELETE (204, repeated 404), error paths inside
  `/storage/` (409 for directory target and missing parent directory,
  404 for directory deletion), routing (501 for `/api/` with reason
  phrase, 403 for a foreign `Host` with reason phrase — also
  regression tests for the `HTTP_STATUS_TEXT` out-of-bounds fix) and
  two requests on one keep-alive connection
- `test_storage_restrict.sh` — modification policy: PUT/POST/DELETE
  allowed inside `/storage/`, forbidden (403) for `/`, `/index.html`,
  `/static/…`, `/files/…` and bare `/storage`; md5 sums of the
  protected files are compared before and after the attempts
- `test_large_bodies.sh` — regression tests for the fixed SIGIO
  deadlock: repeated 1 MiB uploads, 6 MiB PUT/POST (the size that
  hanged the server before the fix) with md5 verification of the
  stored content, 6 MiB download, four concurrent 3 MiB
  upload/download pairs and a check that all worker processes are
  alive afterwards (when started by the runner)
