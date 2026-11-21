# Documentation of tests (directory `tests/`)

Automated tests of the server. Each test area lives in its own
subdirectory with shell scripts and a Makefile; the suites are
curl-based black-box tests — they send http requests to a running
server instance and check status codes, headers, response bodies
and files on disk.

- `http_file_methods/` — tests of file serving for all http methods
  (GET/HEAD/POST/PUT/DELETE), of the /storage/ modification policy
  and of large request/response bodies

General rules for all test directories:

- `make` (or `make test`) in the directory runs the full test set of
  the area; separate targets run individual suites
- the runner script (`run_tests.sh`) builds the server binary if it
  is missing, starts a dedicated server instance with its own config
  (separate port, log file and base directory with fixture files)
  and stops it afterwards, so a development server running from the
  project directory is not disturbed
- the suites can also be run by hand against the development server:
  they default to `http://127.0.0.1:8080` and to `public/` as base
  directory (see `common.sh`)
- prerequisites: running MySQL server (workers open the database
  connection at startup) and `curl`; the test port (18080) must be
  free
- every check prints PASS or FAIL with expected and actual values;
  the script summary and exit code report the total result
- on failure the run directory with the server log and base dir is
  kept in /tmp for inspection, on success it is removed
