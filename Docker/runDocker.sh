#!/bin/bash
/opt/fpp/scripts/init_pre_network
/opt/fpp/scripts/fppd_start

mkdir /run/php
/usr/sbin/php-fpm7.4 --fpm-config /etc/php/7.4/fpm/php-fpm.conf
/usr/sbin/apache2ctl -D FOREGROUND
