#!/bin/sh
set -eu

input_file="${1:?usage: embed.sh INPUT OUTPUT VARNAME GUARD}"
out_file="${2:?usage: embed.sh INPUT OUTPUT VARNAME GUARD}"
var_name="${3:?usage: embed.sh INPUT OUTPUT VARNAME GUARD}"
guard_name="${4:?usage: embed.sh INPUT OUTPUT VARNAME GUARD}"

{
  printf '%s\n' \
    "#ifndef ${guard_name}" \
    "#define ${guard_name}" \
    '' \
    "static const char ${var_name}[] ="
  sed 's/\\/\\\\/g; s/"/\\"/g; s/^/  "/; s/$/\\n"/' "$input_file"
  printf '%s\n' ';' '' '#endif'
} > "$out_file"
