#!/bin/sh
sha1sum="$1"
from="$2"
where="$3"
empty="0000000000000000000000000000000000000000";

cd $from

interesting='^./divine.*(\.c$|\.cpp$|\.h$|\.cc$|\.hh$)';
boring='utility/version.cpp$|\.test\.h$|~$';
if test -e manifest; then
    manifest=`cat manifest`;
else
    if ! test -d _darcs || ! darcs --version > /dev/null; then
        manifest=`find divine -type f` # assume...
    else
        manifest=`darcs query manifest`
    fi
fi

test -z "$old" && old=`cat $where`
if type -p $sha1sum > /dev/null; then
    test -z "$new" && \
        new=`echo "$manifest" | egrep "$interesting" | egrep -v "$boring" | xargs $sha1sum \
        | cut -d' ' -f1 | $sha1sum | cut -d' ' -f1`
else
    old="na"
    new=$empty
fi

echo $new > "$where"

if test "$old" != "$new"; then
    echo "const char *DIVINE_SOURCE_SHA = \"$new\";" > $where.cpp
    echo "const char *DIVINE_BUILD_DATE = \"$(date -u "+%Y-%m-%d, %H:%M UTC")\";" >> $where.cpp
fi
