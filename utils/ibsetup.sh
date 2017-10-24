#! /bin/bash

sudo /etc/init.d/openibd restart
#sudo ifconfig -a
#sudo /sbin/connectx_port_config
sudo ibdev2netdev
sudo ifconfig p5p1 12.12.12.2/24 up
