# Tests of http file methods. The runner builds the server on demand,
# starts a dedicated instance on port 18080 with its own base dir
# and runs the suites against it

.DEFAULT_GOAL := test

test:
	./run_tests.sh

file_methods:
	./run_tests.sh test_file_methods.sh

storage_restrict:
	./run_tests.sh test_storage_restrict.sh

large_bodies:
	./run_tests.sh test_large_bodies.sh

.PHONY: test file_methods storage_restrict large_bodies
