#!/bin/sh

# Documented here
# https://www.kernel.org/doc/Documentation/process/coding-style.rst

INDENT=indent

if which -s gindent
then
    INDENT=gindent
fi

$INDENT -nbad -bap -nbc -bbo -hnl -br -brs -c33 -cd33 -ncdb -ce -ci4 \
    -cli0 -d0 -di1 -nfc1 -i8 -ip0 -l80 -lp -npcs -nprs -npsl -sai \
    -saf -saw -ncs -nsc -sob -nfca -cp33 -ss -ts8 -il1 "$@"