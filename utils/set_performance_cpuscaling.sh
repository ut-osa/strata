#! /bin/bash

MAX=`cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq`

for CPUFREQ in /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor; do [ -f $CPUFREQ ] || continue; echo -n performance > $CPUFREQ; done

for CPUFREQ in /sys/devices/system/cpu/cpu*/cpufreq/scaling_min_freq; do [ -f $CPUFREQ ] || continue; echo -n $MAX > $CPUFREQ; done
for CPUFREQ in /sys/devices/system/cpu/cpu*/cpufreq/cpufreq_min_freq; do [ -f $CPUFREQ ] || continue; echo -n $MAX > $CPUFREQ; done
