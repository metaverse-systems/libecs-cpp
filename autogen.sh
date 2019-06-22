#!/bin/sh

libtoolize \
&& aclocal \
&& autoheader \
&& automake --add-missing -c \
&& autoconf
