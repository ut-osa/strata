Mobile trace Generator (MobiGen)
==================================

Written by Sooman Jeong <77smart@hanyang.ac.kr>

In order to properly analyze IO characateristics of real applications,
we have implemented a tool to capture and replay IO trace of application.
As far as we are aware we are the first to attempt to capture and replay\
IO trace of Android applications. We implemented two tools to meet our needs; 
first tool is called MobiGen which is for capturing IOs of real applications,
and second tool is called MobiBench which replays the captured IO trace.   
  
MobiGen parses result of system call trace of an Android application which is
acquired from strace and creates trace with format applicable for MobiBench. 
In order to supports multi-thread environment in Mobigen, we searched result of
strace file for 'unfinished' and 'resumed' mark and merged corresponding files.
We also added arbitrary open call for those files with no record of open call
in the trace. In an effort to minimize parsing overhead in Mobibench, 
Mobigen orders the system calls within threads and organize parameters in
a simplified form.  Mobigen is written in ruby which is capable of efficiently
manipulating lines of system call traces. Mobibench parses output trace of Mobigen
and replays the IO trace.

Get strace output on target system
-----------------------------------
    # strace -f -t -tt -e trace=file,write,read,close,fsync,fdatasync -p 15476 -o /mnt/ext/twitter


Run MobiGen (On Host PC)
------------------------
	# ruby mobigen.rb [strace_out]
 
* strace_out : trace output

Replay MobiGen
--------------
	# mobibench -p /data/test -g [mobigen_out]



2013-01-10, Sooman Jeong <77smart@hanyang.ac.kr>


