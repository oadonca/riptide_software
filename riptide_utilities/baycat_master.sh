#!/bin/bash

source /opt/ros/kinetic/setup.bash
source ~/osu-uwrt/riptide_software/devel/setup.bash
export ROS_MASTER_URI=http://baycat:11311

exec "$@"
