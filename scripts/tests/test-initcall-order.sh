#!/bin/sh
# Fixture test for scripts/generate_initcall_order.pl: link order across
# archive members, __COUNTER__ order inside one object, every level class,
# and the failures for a malformed name, a repeated counter, an unknown
# level and a failing NM.
set -eu

PERL=${PERL:-perl}
source_tree=$(CDPATH='' cd -- "$(dirname -- "$0")/../.." && pwd)
generator=$source_tree/scripts/generate_initcall_order.pl
temporary_dir=$(mktemp -d)
trap 'rm -f "$temporary_dir"/*; rmdir "$temporary_dir"' EXIT HUP INT TERM

# The fake NM prints the fixture file named on its command line.
cat > "$temporary_dir/fake-nm" <<'EOF'
#!/bin/sh
set -eu
test "$1" = --defined-only
test "$2" = --quiet
if test "$3" = "${FAIL_INPUT:-}"; then
	exit 7
fi
cat "$3"
EOF
chmod +x "$temporary_dir/fake-nm"

expect_failure() {
	message=$1
	shift
	if NM="$temporary_dir/fake-nm" "$@" > "$temporary_dir/invalid.lds" \
		2> "$temporary_dir/error"; then
		echo "generator accepted input that must fail: $message" >&2
		exit 1
	fi
	grep -Fq "$message" "$temporary_dir/error"
}

cat > "$temporary_dir/ordered" <<'EOF'
first.o:
-------- d __initcall__kmod_a_first__2_20_same6
-------- d __initcall__kmod_a_first__1_10_early_init0
second.o:
-------- d __initcall__kmod_b_second__1_10_same6
-------- d __initcall__kmod_b_second__2_30_rootfs_initrootfs
-------- d __initcall__kmod_b_second__3_40_console_initcon
-------- d __initcall__kmod_b_second__4_50_security_initsec
-------- d __initcall__kmod_b_second__5_60_sync_init6s
EOF

NM="$temporary_dir/fake-nm" "$PERL" "$generator" \
	"$temporary_dir/ordered" > "$temporary_dir/order.lds"
grep -Fq '.initcall0.init..__initcall__kmod_a_first__1_10_early_init0' "$temporary_dir/order.lds"
grep -Fq '.initcallrootfs.init..__initcall__kmod_b_second__2_30_rootfs_initrootfs' "$temporary_dir/order.lds"
grep -Fq '.con_initcall.init..__initcall__kmod_b_second__3_40_console_initcon' "$temporary_dir/order.lds"
grep -Fq '.security_initcall.init..__initcall__kmod_b_second__4_50_security_initsec' "$temporary_dir/order.lds"
grep -Fq '.initcall6s.init..__initcall__kmod_b_second__5_60_sync_init6s' "$temporary_dir/order.lds"
# Level 6 keeps first.o before second.o although second.o's counter is lower.
grep -F '.initcall6.init..' "$temporary_dir/order.lds" > "$temporary_dir/level6"
test "$(sed -n 1p "$temporary_dir/level6" | grep -c kmod_a_first)" -eq 1
test "$(sed -n 2p "$temporary_dir/level6" | grep -c kmod_b_second)" -eq 1

cat > "$temporary_dir/malformed" <<'EOF'
-------- d __initcall_bad
EOF
expect_failure 'malformed initcall symbol' \
	"$PERL" "$generator" "$temporary_dir/malformed"

cat > "$temporary_dir/duplicate" <<'EOF'
-------- d __initcall__kmod_a_first__1_10_same6
-------- d __initcall__kmod_a_first__1_11_other6
EOF
expect_failure 'duplicate initcall counter' \
	"$PERL" "$generator" "$temporary_dir/duplicate"

cat > "$temporary_dir/unknown-level" <<'EOF'
-------- d __initcall__kmod_a_first__1_10_unknown8
EOF
expect_failure 'unknown initcall level' \
	"$PERL" "$generator" "$temporary_dir/unknown-level"

FAIL_INPUT="$temporary_dir/ordered"
export FAIL_INPUT
expect_failure 'failed for' \
	"$PERL" "$generator" "$temporary_dir/ordered"

printf 'initcall order fixtures passed\n'
