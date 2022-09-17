#! /usr/bin/env bash

last_dir="$PWD"
cd "$(dirname "$0")"
tmpdir='../build/out/seqtmp'
for i in `seq 1 100000`
do
  test ! -d "$tmpdir" && mkdir -p "$tmpdir"
  mkdir "$tmpdir/$i"
  rmdir "$tmpdir/$i"
done
rmdir "$tmpdir"
cd "$last_dir"

