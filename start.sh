#!/bin/sh
echo "protocol device {}" > /etc/calico/confd/config/bird.cfg
echo "protocol device {}" > /etc/calico/confd/config/bird6.cfg

bird -R -s bird.ctl -d -c /etc/calico/confd/config/bird.cfg

