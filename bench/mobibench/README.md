Mobile benchmark tool (mobibench)
================================

* Maintainer : Sooman Jeong (smartzzz77@gmail.com)
* Contributor : Kisung Lee (kisunglee@hanyang.ac.kr), Seongjin Lee (insight@hanyang.ac.kr), Dongil Park (idoitlpg@hanyang.ac.kr), Jungwoo Hwang (tearoses@hanyang.ac.kr), and Chanhyun Park (chanhyun0708@gmail.com, Hankeun Son (hself@hanyang.ac.kr)

### Reference: 
 * Sooman Jeong, Kisung Lee, Jungwoo Hwang, Seongjin Lee, Youjip Won, "AndroStep: Android Storage Performance Analysis Tool", The 1st European Workshop on Mobile Engineering (ME'13), Feb. 26, 2013, Aachen, Germany 
<http://www.esos.hanyang.ac.kr/files/publication/conferences/international/AndroStep.pdf>

 * Sooman Jeong, Kisung Lee, Jungwoo Hwang, Seongjin Lee, Youjip Won, "Framework for Analyzing Android I/O Stack Behavior: from Generating the Workload to Analyzing the Trace", Future Internet 2013, 5(4), Special Issue for "Mobile Engineering², MDPI(ISSN 1999-5903), 591-610; doi:10.3390/fi5040591 
<http://www.mdpi.com/1999-5903/5/4/591/pdf>

### Acknowledgement:
 * This work is supported by IT R&D program MKE/KEIT (No. 10041608, Embedded System Software for New-memory based Smart Device). 

### Release
#####Version 1.04
Followings are updated for the performance rank system.
 * Customized benchmark results can be registered to the performance rank server.
 * Average performance of the users with the same device is shown in the rank page.
 * The best, median, and the worst performance of a device is shown in the details page under the rank.
 * eMMC serial number, memory size, and kernel version for each device is shown in the details page.


MobiBench (Application version)
-----------------------------------
Mobibench allows to measure IO performance and SQLite of Android filesystem, and also allows to measure user specified set of tests. Here we provide some scenarios as a guide to use our Mobibench Benchmark tool. 
One day, Alice got curious about IO performance of Android filesystem, especially performance of random and sequential read/write IO. Alice finds File IO button in Measure tab. Alice presses File IO button. Progress bar at the bottom of the screen shows progress and percentile to completion of tests. File IO button initiates sequential and random read/write IO benchmarks simultaneously. After few seconds, Alice receives a result of the benchmark on a pop-up window with throughput measured in KB/s, CPU utilization in percentage, and number of context switches made in the tests. Description under CPU utilization shows percentage of busy, IO wait, and idle time during the tests. Under Context switch, the number of switches describes total number of context switches made and number of voluntary context switches in parenthesis. Alice finds benchmark settings applied in File IO on the Setting tab. 
When Bob heard news on Mobibench from Alice, Bob paid visit to Alice. Bob knew different Android platform benchmark tools; however, they did not provide features to run SQLite separate to File IO benchmarks nor configure IO modes and SQLite journal mode settings. Bob presses SQLite button on Measure tab. SQLite button runs set of DB operations including insert, update, and delete as a transaction. Just like File IO benchmark button, SQLite button shows throughput measured in transactions per second, CPU utilization, and number of context switches on a pop-up window. Bob repeats measuring performance of SQLite while changing journal modes from the Setting tab. Bob finds history of his test results on History tab. 

Here we briefly describe what features are provided in each tabs.

Measure Tab
-------------
There are four buttons in the Measure tab. The result of the run describes throughput measured in KB/s or TPS depending on the benchmark, CPU utilization, and number of context switches. 
ALL: Runs File IO and SQLite simultaneously.
File IO: Runs sequential and random read/write operations simultaneously. 
SQLite: Runs transactions of insert, update, and delete SQLite operations 
Custom: Runs set of tests that an user chose from the Setting tab.

History Tab
-------------
This tab keeps record of results of previous runs of benchmarks.

Setting Tab
-------------
The tab provides set of detailed configurable options for each benchmark. It provides options to change partition, number of threads in a benchmark, IO size, File size, IO modes, and journal modes. Details are as follows.
### Partition
 Changes target partition of the device from ``/data’’ to following partitions.
 * /data: /data partition of internal storage
 * /sdcard: /sdcard partition of internal storage
 * /extSdCard: /extSdCard partition of external storage
      
### Number of Threads
 Changes the number of concurrent threads in a benchmark. Default mode runs in single thread and it allows to increase the number up to 100 threads
      
### Custom check box
 When checkbox is set, an user can chose specific tests in a File IO and SQLite benchmarks.
      
### File size
 Changes the size of file size in File IO test. Since write to a NAND Flash media is slower compared to read operation, we use different default file size for write and read operation. Default file size for write and read is 1MB and 32MB, respectively. 
      
### IO Size
 Changes the size of IO issued in File IO benchmark. Default size is 4KB
      
### Mode(File IO)
 Changes synchronization mode for File IO benchmark. Default mode run in O_DIRECT mode. List of synchronization modes available are as follows.
  * Normal: Buffered IO
  * O_SYNC: Synchronous IO
  * fsync: Each write() call is synchronously written to the storage device via fsync() system call.
  * O_DIRECT: Direct IO
  * Sync+direct : Each write() call is synchronously written to the storage device via fsync() system call in O_DIRECT mode.
  * mmap: On this option, benchmark program mmaps the created file to virtual address space. Then, the program copies the specified size record, e.g. 4KB, to the mmaped memory region. Upon completing copy of all memory copy, this mode flushes file through msync() to storage device.
  * mmap+MS_ASYNC: Uses mmap mode with MS_ASYNC flag. Each memory copy of an IO size is synchronized to storage device with msync() system call asynchronously. 
  * mmap+MS_SYNC: Uses mmap mode with MS_SYNC flag. Each memory copy of an IO size is synchronized to storage device with msync() system call synchronously.
  * fdatasync: Each write() call is synchronously written to the storage device via fdatasync() system call.
                  
### Transaction(SQLite)
 Sets number of SQLite transactions, where default number of transactions is 100. Maximum number of transaction is 10,000. 
                  
### Mode(SQLite)
 Changes SQLite synchronous mode. Default mode in SQLite test is FULL.
  * OFF: This mode does not use synchronization mechanism to guarantee that all data is written to the storage device.
  * NORMAL: SQLite uses synchronization mechanism to sync the data to the storage device at certain critical moments, but less often than in FULL mode.
  * FULL: SQLite database is forced to use synchronized via fsync() system call to ensure that all content is safely written to the storage before processing following IO requests.
                          
### Journal(SQLite)
 Changes SQLite journal mode. Default mode of SQLite is TRUNCATE mode. For detailed explanation of each mode please refer to SQLite homepage.
  * DELETE: Delete mode
  * TRUNCATE: Truncate mode
  * PERSIST: Persist mode
  * WAL: Write Ahead Logging mode
  * MEMORY: Memory mode
  * OFF: Off mode
                                      
                                      

Build shell version
--------------------
    # cd shell && make


Usage (shell version)
----------------------
	# mobibench [-p pathname] [-f file_size_Kb] [-r record_size_Kb] [-a access_mode] [-h]
                    [-y sync_mode] [-t thread_num] [-d db_mode] [-n db_transcations]
                    [-j SQLite_journalmode] [-s SQLite_syncmode] [-g replay_script] [-q]
                                     
                                     
* -p  set path name (default=./mobibench)
* -f  set file size in KBytes (default=1024)
* -r  set record size in KBytes (default=4)
* -a  set access mode (0=Write, 1=Random Write, 2=Read, 3=Random Read) (default=0)
* -y  set sync mode (0=Normal, 1=O_SYNC, 2=fsync, 3=O_DIRECT, 4=Sync+direct,
                     5=mmap, 6=mmap+MS_ASYNC, 7=mmap+MS_SYNC 8=fdatasync) (default=0)
* -t  set number of thread for test (default=1)
* -d  enable DB test mode (0=insert, 1=update, 2=delete)
* -n  set number of DB transaction (default=10)
* -j  set SQLite journal mode (0=DELETE, 1=TRUNCATE, 2=PERSIST, 3=WAL, 4=MEMORY, 
                               5=OFF) (default=1)
* -s  set SQLite synchronous mode (0=OFF, 1=NORMAL, 2=FULL) (default=2)
* -g  set replay script (output of MobiGen)
* -q  do not display progress(%) message                                                           			

