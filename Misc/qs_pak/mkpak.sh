#!/bin/sh
#
# Copyright (c) 2014, Sander van Dijk <a.h.vandijk@gmail.com>
#
# Permission to use, copy, modify, and/or distribute this software for any
# purpose with or without fee is hereby granted, provided that the above
# copyright notice and this permission notice appear in all copies.
#
# THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
# WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
# MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
# ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
# WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
# ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
# OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

error() {
	echo "$(basename "$0"): $*" >&2
	exit 1
}

assert_valid_stdout() {
	if test -t 1
	then
		error "Usage: $(basename "$0") [file ...] > output.pak"
	fi
}

assert_valid_file() {
	if test ! -e "$1"
	then
		error "$1: No such file"
	fi
	if test ! -f "$1"
	then
		error "$f: Not a regular file"
	fi
	if test ! -r "$1"
	then
		error "$1: Permission denied"
	fi
	if test $(echo -n "$1" | wc -c) -gt 55
	then
		error "$1: Name too long"
	fi
}

assert_valid_int32() {
	if test $1 -lt -2147483648 -o $1 -gt 2147483647
	then
		error "Too much data"
	fi
}

octal() {
	if test $1 -gt 7
	then
		octal $(expr $1 / 8)
	fi
	echo -n $(expr $1 % 8) 
}

byte() {
	echo -en \\0$(octal $1)
}

little_endian_uint32() {
	byte $(expr $1 % 256)
	byte $(expr $1 / 256 % 256)
	byte $(expr $1 / 65536 % 256)
	byte $(expr $1 / 16777216 % 256)
}

little_endian_int32() {
	if test $1 -lt 0
	then
		little_endian_uint32 $(expr $1 + 4294967296)
	else
		little_endian_uint32 $1
	fi
}

zero_padding() {
	if test $1 -lt 1
	then
		return
	fi
	byte 0
	zero_padding $(expr $1 - 1)
}

header() {
	echo -n PACK
	little_endian_int32 $1
	little_endian_int32 $2
}

directory_entry() {
	echo -n "$1"
	zero_padding $(expr 56 - $(echo -n "$1" | wc -c))
	little_endian_int32 $2
	little_endian_int32 $3
}

assert_valid_stdout

directory_offset=12
directory_size=0
for file in "$@" 
do
	assert_valid_file "$file"
	file_offset=$directory_offset
	assert_valid_int32 $file_offset
	file_size=$(wc -c < "$file")
	assert_valid_int32 $file_size
	directory_offset=$(expr $directory_offset + $file_size)
	assert_valid_int32 $directory_offset
	directory_size=$(expr $directory_size + 64)
	assert_valid_int32 $directory_size
done

header $directory_offset $directory_size

for file in "$@"
do
	cat "$file"
done

file_offset=12
for file in "$@"
do
	file_size=$(wc -c < "$file")
	directory_entry "$file" $file_offset $file_size
	file_offset=$(expr $file_offset + $file_size)
done
