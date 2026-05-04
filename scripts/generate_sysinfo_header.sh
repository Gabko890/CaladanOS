#!/bin/sh

set -eu

output_path="$1"
build_label="${2:-}"
git_branch="${3:-}"
git_commit="${4:-}"

if [ -z "$build_label" ]; then
	build_label="build"
fi

git_dir="$(pwd)"
if [ -z "$git_branch" ]; then
	git_branch="$(git -c safe.directory="$git_dir" rev-parse --abbrev-ref HEAD 2>/dev/null || printf "unknown")"
fi
if [ -z "$git_commit" ]; then
	git_commit="$(git -c safe.directory="$git_dir" rev-parse --short=12 HEAD 2>/dev/null || printf "unknown")"
fi

build_datetime="$(date -u "+%Y-%m-%dT%H:%M:%SZ")"
kernel_version="$(if [ -f version.txt ]; then head -n 1 version.txt; else printf "0.0"; fi)"

escape_c_string() {
	printf "%s" "$1" | sed 's/\\/\\\\/g; s/"/\\"/g'
}

cat > "$output_path" <<EOF
#ifndef SYSINFO_VALUES_H
#define SYSINFO_VALUES_H

#define SYSINFO_GIT_BRANCH "$(escape_c_string "$git_branch")"
#define SYSINFO_GIT_COMMIT "$(escape_c_string "$git_commit")"
#define SYSINFO_BUILD_DATETIME "$(escape_c_string "$build_datetime")"
#define SYSINFO_BUILD_LABEL "$(escape_c_string "$build_label")"
#define SYSINFO_KERNEL_VERSION "$(escape_c_string "$kernel_version")"

#endif
EOF
