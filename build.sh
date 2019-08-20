# clang=clang-7
clang="E:\Program Files\LLVM\bin\clang.exe"
warnings="-Wall -Werror -Wno-invalid-token-paste -Wno-unused-function"
"$clang" -g $warnings hash.c && \
"$clang" -E $warnings hash.c > preproc.c
