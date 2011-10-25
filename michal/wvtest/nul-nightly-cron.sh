#!/bin/bash
set -e
PATH=$HOME/bin:$PATH
ret=0
log=$(date '+nul_%F_%T.log')
cd ~/nul-nightly
if ! nul-nightly.sh > $log 2>&1; then
    ret=1
    (
	echo "Pipe this mail to 'nul/michal/wvtest/wvtestrun cat' to get more human readable formating."
	echo
	cat $log
    ) | mail -s 'NUL nighly build/test failed!' sojka@os
fi
cat $log  # Mail the log to me (backup)

cat nul_*.log | wvperf2html.py > performance.html && mv performance.html ~/public_html/nul/

exit $ret
