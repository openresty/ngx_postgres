#!/bin/bash

# this file is mostly meant to be used by the author himself.

root=`pwd`
version=$1
home=~
force=$2

        #--with-cc=gcc46 \

ngx-build $force $version \
            --with-cc-opt="-I$PCRE_INC -I$OPENSSL_INC" \
            --with-ld-opt="-L$PCRE_LIB -L$OPENSSL_LIB -Wl,-rpath,$PCRE_LIB:$LIBDRIZZLE_LIB:$OPENSSL_LIB" \
            --with-http_ssl_module \
            --with-ipv6 \
          --add-module=$root/../echo-nginx-module \
          --add-module=$root/../lua-nginx-module \
          --add-module=$root/../eval-nginx-module \
          --add-module=$root/../rds-json-nginx-module \
          --add-module=$root/../ndk-nginx-module \
          --add-module=$root/../set-misc-nginx-module \
          --add-module=$root/../form-input-nginx-module \
          --add-module=$root/../coolkit-nginx-module \
          --add-module=$home/work/nginx/ngx_http_auth_request_module-0.2/ \
          --add-module=$root $opts \
          --with-select_module \
          --with-poll_module \
          --with-threads \
          --with-debug
          #--add-module=$home/work/ngx_http_auth_request-0.1 #\
          #--with-rtsig_module
          #--with-cc-opt="-g3 -O0"
          #--add-module=$root/../echo-nginx-module \
  #--without-http_ssi_module  # we cannot disable ssi because echo_location_async depends on it (i dunno why?!)

