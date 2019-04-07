#!/usr/bin/env bash

cd /u8/l62zhou/cs350-os161/os161-1.99/kern/compile/ASST3

echo "$(tput setaf 4)$(tput setab 7)******bmake depend*******$(tput sgr 0)"
bmake depend >/dev/null
echo ""

echo "$(tput setaf 4)$(tput setab 7)******bmake*******$(tput sgr 0)"
bmake
echo "make complete, please check result above"
echo ""

echo "$(tput setaf 4)$(tput setab 7)******bmake install*******$(tput sgr 0)"
bmake install


if [ "$1" != "" ]; then
    cd /u8/l62zhou/cs350-os161/os161-1.99/
    echo "$(tput setaf 1)$(tput setab 7)******bmake [user program]*******$(tput sgr 0)"
    bmake >/dev/null
    echo ""

    echo "$(tput setaf 1)$(tput setab 7)******bmake install [user program]*******$(tput sgr 0)"
    bmake install >/dev/null
    echo ""
else
    echo ""
fi
