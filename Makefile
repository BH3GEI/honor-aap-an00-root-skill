.PHONY: check build run verify mirrors

check:
	./scripts/check_env.sh --require-target

build:
	./scripts/build.sh

run:
	./scripts/run_root.sh

verify:
	./scripts/verify_root.sh

mirrors:
	./scripts/sync_mirrors.sh
