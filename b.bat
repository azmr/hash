@echo off
pushd %~dp0
set warnings=-Wall -Werror -Wno-invalid-token-paste -Wno-unused-function
clang -g %warnings% hash.c -o hash_test.exe
clang -E %warnings% hash.c > preproc.c
hash_test.exe
popd
