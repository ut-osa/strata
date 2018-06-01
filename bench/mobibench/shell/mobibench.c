#define _GNU_SOURCE
/* mobibench.c for mobile benchmark tool(mobibench)
 *
 * Copyright (C) 2012 ESOS lab, Hanyang University
 *
 * History
 * 2012. 1 created by Kisung Lee <kisunglee@hanyang.ac.kr>
 * 2012. 8 modified by Sooman Jeong <77smart@hanyang.ac.kr>
 * Jun 03, 2013 version 1.0.0 by Sooman Jeong
 * Jul 02, 2013 forced checkpointing for WAL mode by Sooman Jeong
 * Aug 22, 2013 fix buffer-overflow on replaying mobigen script by Sooman Jeong [version 1.0.1]
 * Dec 31, 2013 Modified to print Write error code by Seongjin Lee [version 1.0.11]
 * Mar 26, 2014 Collect latency data for each I/O by Seongjin Lee [version 1.0.2]
 * Nov 20, 2014 Print IOPS on every second by Seongjin Lee [version 1.0.3]
 * Nov 20, 2014 Percentage overlap in Random workload by Jinsoo Yoo [version 1.0.4]
 * Mar 28, 2015 fdatasync sync mode for file write by Hankeun Son [version 1.0.5]
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>


#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>
#include <time.h>
#include <string.h> 

#include <sys/mman.h>
#include <errno.h>
#include <pthread.h>
#include <sys/syscall.h>
#include <dirent.h>


/* for sqlite3 */
#include "sqlite3.h"

#define VERSION_NUM	"1.0.4"

/* for Strata FS */
#include <mlfs/mlfs_interface.h>

//#define DEBUG_SCRIPT

#ifdef DEBUG_SCRIPT
#define SCRIPT_PRINT printf
#else
#define SCRIPT_PRINT(fmt,args...) 
#endif

#ifdef ANDROID_APP
#include <android/log.h>

#define printf(fmt,args...)  __android_log_print(4  ,NULL, fmt, ##args)
int progress = 0;
float cpu_active = 0;
float cpu_idle = 0;
float cpu_iowait = 0;
int cs_total = 0;
int cs_voluntary = 0;
float throughput = 0;
float tps = 0;

#endif

/* Defs */
#define SIZE_100MB 104857600
#define SIZE_1MB 1048576
#define SIZE_4KB 4096
#define SIZE_1KB 1024
#define MAX_THREADS 100

#define INSERT_STR "aaaaaaaaaabbbbbbbbbbccccccccccddddddddddeeeeeeeeeeffffffffffgggggggggghhhhhhhhhhiiiiiiiiiijjjjjjjjjj"
#define UPDATE_STR "ffffffffffgggggggggghhhhhhhhhhiiiiiiiiiijjjjjjjjjjaaaaaaaaaabbbbbbbbbbccccccccccddddddddddeeeeeeeeee"

typedef enum
{
  MODE_WRITE,
  MODE_RND_WRITE,
  MODE_READ,
  MODE_RND_READ,
} file_test_mode_t;

typedef enum
{
	NORMAL,
  OSYNC,
  FSYNC,
  ODIRECT,
  SYDI,
  MMAP,
  MMAP_AS,
  MMAP_S,
  FDATASYNC,
} file_sync_mode_t;

typedef enum
{
	NONE,
	READY,
	EXEC,
	END,
	ERROR,
} thread_status_t;

struct script_entry {
	int thread_num;
	long long time;
	char* cmd;
	char* args[3];
	int arg_num;
};

struct script_thread_time {
	int thread_num;
	int count;
	int started;
	int ended;
	long long start;
	long long end;
	long long io_time;
	int io_count;
	int write_size;
	int read_size;
};

struct script_thread_info {
	int thread_num;
	int line_count;
	int open_count;
};

struct script_fd_conv {
	int* fd_org;
	int* fd_new;
	int index;
};

/* Globals */
long long kilo64; //file size
long long reclen;
long long real_reclen;
long long numrecs64;
int g_access, g_sync;
char* maddr;
long long filebytes64;
int num_threads;
char pathname[128] = {0, };
int db_test_enable;
int db_mode;
int db_transactions;
int db_journal_mode;
int db_sync_mode;
int db_init_show = 0;
int db_interval = 0;
int g_state = 0;
char* g_err_string;
int b_quiet = 0;
int b_replay_script = 0;
char* script_path;
struct script_entry* gScriptEntry = {0, };
int script_thread_num=0;
struct script_thread_time* gScriptThreadTime = {0, };
struct script_fd_conv gScriptFdConv = {0, };
long long time_start;
char* script_write_buf;
char* script_read_buf;
int Latency_state = 0; // flag for printing latency
FILE* Latency_fp; // latency output
char REPORT_Latency[200]; // file name for latency output
int print_IOPS = 0; // flag for printing IOPS every second
FILE* pIOPS_fp; // output for print IOPS every second 
char REPORT_pIOPS[200]; // file name for IOPS output

int overlap_ratio = 0; // overlap ratio for random write

thread_status_t thread_status[MAX_THREADS] = {0, };
pthread_mutex_t thread_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t thread_cond1 = PTHREAD_COND_INITIALIZER;
pthread_cond_t thread_cond2 = PTHREAD_COND_INITIALIZER;
pthread_cond_t thread_cond3 = PTHREAD_COND_INITIALIZER;

pthread_mutex_t fd_lock = PTHREAD_MUTEX_INITIALIZER;

long long get_current_utime(void); // get current time
long long get_relative_utime(long long start); // and relative time

void clearState(void)
{
	g_state = NONE;
	g_err_string = "No error";
}

void setState(int state, char* err_str)
{
	printf("%s: %d\n", __func__, state);
	if(g_state != ERROR)
	{
		g_state = state;
		if(state == ERROR)
		{
			g_err_string = err_str;
		}
	}
}

int getState(void)
{
	return g_state;
}

void print_time(struct timeval T1, struct timeval T2)
{
	long sec,usec;
	double time;
	double rate;
	double latency;
	
	time_t t;
	
	if(T1.tv_usec > T2.tv_usec)
	{
		sec = T2.tv_sec - T1.tv_sec -1;
		usec = 1000000 + T2.tv_usec - T1.tv_usec;
	}
	else
	{
		sec = T2.tv_sec - T1.tv_sec;
		usec = T2.tv_usec - T1.tv_usec;
	}

	time = (double)sec + (double)usec/1000000;
	
	if(db_test_enable == 0)
	{
		rate = kilo64*1024*num_threads/time;
		printf("[TIME] :%8ld sec %06ldus. \t%.0f B/sec, \t%.2f KB/sec, \t%.2f MB/sec.",sec,usec, rate, rate/1024, rate/1024/1024);
		if(Latency_state==1) fprintf(Latency_fp,"[TIME] :%8ld sec %06ldus. \t%.0f B/sec, \t%.2f KB/sec, \t%.2f MB/sec.",sec, usec, rate, rate/1024, rate/1024/1024);
		if(g_access == MODE_RND_WRITE || g_access == MODE_RND_READ)
		{
			printf(" %.2f IOPS(%dKB) ", rate/1024/reclen, (int)reclen);
			if(Latency_state==1) fprintf(Latency_fp," %.2f IOPS(%dKB) ", rate/1024/reclen, (int)reclen);
		}
		printf("\n");
#ifdef ANDROID_APP
		if(g_access == MODE_RND_WRITE || g_access == MODE_RND_READ)
		{
			throughput = (float)rate/1024/reclen;
		}
		else
		{
			throughput = (float)rate/1024;
		}
#endif
	}
	else
	{
		rate = db_transactions*num_threads/time;
		latency = time * 1e6 / db_transactions;
		printf("[TIME] :%8ld sec %06ldus. Transaction latency %.2fus, "
				"%.2f Transactions/sec\n",sec, usec, latency, rate);
#ifdef ANDROID_APP
		tps = (float)rate;
#endif		
	}
	
}

#define DEF_PROCSTAT_BUFFER_SIZE (1 << 10) /* 1 KBytes (512 + 512 ^^) */ 

#define START_CPU_CHECK 0
#define END_CPU_CHECK 1

unsigned long s_CPUTick[2][6]; 

void cpuUsage(int startEnd)
{
	const char *s_ProcStat = "/proc/stat"; 
	const char *s_CPUName = "cpu "; 
	int s_Handle, s_Check, s_Count; 
	char s_StatBuffer[ DEF_PROCSTAT_BUFFER_SIZE ]; 
	char *s_String; 
	float s_DiffTotal; 
	unsigned long active_tick, idle_tick, wait_tick;
	
		s_Handle = open(s_ProcStat, O_RDONLY); 
		
		if(s_Handle >= 0) 
		{ 
				s_Check = read(s_Handle, &s_StatBuffer[0], sizeof(s_StatBuffer) - 1); 
			
				s_StatBuffer[s_Check] = '\0'; 

				s_String = strstr(&s_StatBuffer[0], s_CPUName); /* Total CPU entry */ 
				
				//printf("s_String=%s\n", s_String);

				if(s_String) 
				{ 

					s_Check = sscanf(s_String, "cpu %lu %lu %lu %lu %lu", &s_CPUTick[startEnd][0], &s_CPUTick[startEnd][1], &s_CPUTick[startEnd][2], &s_CPUTick[startEnd][3], &s_CPUTick[startEnd][4]); 

					//printf("s_Check=%d\n", s_Check);

					if(s_Check == 5) 
					{ 

						for(s_Count = 0, s_CPUTick[startEnd][5] = 0lu; s_Count < 5;s_Count++)
							s_CPUTick[startEnd][5] += s_CPUTick[startEnd][s_Count]; 
					}
					
					//printf("[CPU] 0=%ld, 1=%ld, 2=%ld, 3=%ld, 4=%ld, \n", s_CPUTick[startEnd][0], s_CPUTick[startEnd][1], s_CPUTick[startEnd][2], s_CPUTick[startEnd][3], s_CPUTick[startEnd][4]);
				}
		}
		else
		{
			printf("%s open failed.\n", s_ProcStat);
			return;
		}
		
		//printf("[CPU] 0=%ld, 1=%ld, 2=%ld, 3=%ld, 4=%ld, \n", s_CPUTick[startEnd][0], s_CPUTick[startEnd][1], s_CPUTick[startEnd][2], s_CPUTick[startEnd][3], s_CPUTick[startEnd][4]);
		
		if(startEnd == END_CPU_CHECK)
		{
			s_DiffTotal = (float)(s_CPUTick[END_CPU_CHECK][5] - s_CPUTick[START_CPU_CHECK][5]); 
			active_tick = (s_CPUTick[END_CPU_CHECK][0] - s_CPUTick[START_CPU_CHECK][0]) + (s_CPUTick[END_CPU_CHECK][1] - s_CPUTick[START_CPU_CHECK][1]) + (s_CPUTick[END_CPU_CHECK][2] - s_CPUTick[START_CPU_CHECK][2]);
			idle_tick = (s_CPUTick[END_CPU_CHECK][3] - s_CPUTick[START_CPU_CHECK][3]);
			wait_tick = (s_CPUTick[END_CPU_CHECK][4] - s_CPUTick[START_CPU_CHECK][4]);
						
			//printf("[CPU TICK] Active=%ld, Idle=%ld, IoWait=%ld\n",active_tick, idle_tick, wait_tick);
			printf("[CPU] : Active,Idle,IoWait : %1.2f %1.2f %1.2f\n",
							(float)( (float)(active_tick * 100lu) / s_DiffTotal ),
							(float)( (float)(idle_tick * 100lu) / s_DiffTotal ), 
							(float)( (float)(wait_tick * 100lu) / s_DiffTotal ) ); 
#ifdef ANDROID_APP
			cpu_active = (float)( (float)(active_tick * 100lu) / s_DiffTotal );
			cpu_idle = (float)( (float)(idle_tick * 100lu) / s_DiffTotal );
			cpu_iowait = (float)( (float)(wait_tick * 100lu) / s_DiffTotal );
#endif
		}
		
		close(s_Handle);			
}

#define NN 312
#define MM 156
#define MATRIX_A 0xB5026F5AA96619E9ULL
#define UM 0xFFFFFFFF80000000ULL /* Most significant 33 bits */
#define LM 0x7FFFFFFFULL /* Least significant 31 bits */


/* The array for the state vector */
static unsigned long long mt[NN]; 
/* mti==NN+1 means mt[NN] is not initialized */
static int mti=NN+1; 

void init_genrand64(unsigned long long seed)
{
    mt[0] = seed;
    for (mti=1; mti<NN; mti++) 
        mt[mti] =  (6364136223846793005ULL * (mt[mti-1] ^ (mt[mti-1] >> 62)) + mti);
}

unsigned long long genrand64_int64(void)
{
    int i;
    unsigned long long x;
    static unsigned long long mag01[2]={0ULL, MATRIX_A};

    if (mti >= NN) { /* generate NN words at one time */

        /* if init_genrand64() has not been called, */
        /* a default initial seed is used     */
        if (mti == NN+1) 
            init_genrand64(5489ULL); 

        for (i=0;i<NN-MM;i++) {
            x = (mt[i]&UM)|(mt[i+1]&LM);
            mt[i] = mt[i+MM] ^ (x>>1) ^ mag01[(int)(x&1ULL)];
        }
        for (;i<NN-1;i++) {
            x = (mt[i]&UM)|(mt[i+1]&LM);
            mt[i] = mt[i+(MM-NN)] ^ (x>>1) ^ mag01[(int)(x&1ULL)];
        }
        x = (mt[NN-1]&UM)|(mt[0]&LM);
        mt[NN-1] = mt[MM-1] ^ (x>>1) ^ mag01[(int)(x&1ULL)];

        mti = 0;
    }
  
    x = mt[mti++];

    x ^= (x >> 29) & 0x5555555555555555ULL;
    x ^= (x << 17) & 0x71D67FFFEDA60000ULL;
    x ^= (x << 37) & 0xFFF7EEE000000000ULL;
    x ^= (x >> 43);

    return x;
}

void init_by_array64(unsigned long long init_key[],
		     unsigned long long key_length)
{
    unsigned long long i, j, k;
    init_genrand64(19650218ULL);
    i=1; j=0;
    k = (NN>key_length ? NN : key_length);
    for (; k; k--) {
        mt[i] = (mt[i] ^ ((mt[i-1] ^ (mt[i-1] >> 62)) * 3935559000370003845ULL))
          + init_key[j] + j; /* non linear */
        i++; j++;
        if (i>=NN) { mt[0] = mt[NN-1]; i=1; }
        if (j>=key_length) j=0;
    }
    for (k=NN-1; k; k--) {
        mt[i] = (mt[i] ^ ((mt[i-1] ^ (mt[i-1] >> 62)) * 2862933555777941757ULL))
          - i; /* non linear */
        i++;
        if (i>=NN) { mt[0] = mt[NN-1]; i=1; }
    }

    mt[0] = 1ULL << 63; /* MSB is 1; assuring non-zero initial array */ 
}

char j_p_name[100] = {0, };
unsigned int j_nr_switches[2] = {0, };
int storage_switches[2][1000] = {0, };
int storage_count=0;

void get_path(pid_t j_pid, pid_t j_tid)
{
	sprintf(j_p_name, "/proc/%d/task/%d/sched", j_pid, j_tid);
//	strcat(j_p_path,j_p_name);
//	strcat(j_p_path,"/sched");
//	printf("%s %s\n", __func__, j_p_name);
}

void get_con_switches()
{
	storage_switches[0][storage_count] = j_nr_switches[0];
	storage_switches[1][storage_count] = j_nr_switches[1];
	storage_count++;
}

void print_con_switches()
{

//	int i;	
//	for(i =0 ; i< storage_count ; i++)
//		printf("%d [th] \t\t N1 %u \t\t N2 %u \n",i/2,storage_switches[0][i],storage_switches[1][i]);

	printf( "[CON SWITCHES] : %d %d",storage_switches[0][1] - storage_switches[0][0], storage_switches[1][1] - storage_switches[1][0]);
	printf( "\n");
#ifdef ANDROID_APP
	cs_total = storage_switches[0][1] - storage_switches[0][0];
	cs_voluntary = storage_switches[1][1] - storage_switches[1][0];
#endif
}


int single_get_nr_switches(void)
{
	int j_context_fd;
	char j_buf[3072], *j_token[2];
	char j_dummy[128];
	int ret;
	
	j_context_fd = open(j_p_name, O_RDONLY);
	
	if(j_context_fd < 0)
	{
		printf("\n\n open %s, FD: %d\n\n", j_p_name, j_context_fd);
		perror("Fail to open ");
		return -1;
	}

	ret = read(j_context_fd, j_buf, sizeof(j_buf));
	if(ret < 0)
	{
		perror("Fail to read");
		close(j_context_fd);
		return -2;
	}

	j_token[0] = strstr(j_buf, "nr_switches");
	j_token[1] = strstr(j_buf, "nr_voluntary_switches");

	sscanf(j_token[0], "%s %s %u", j_dummy, j_dummy, &j_nr_switches[0]);
	sscanf(j_token[1], "%s %s %u", j_dummy, j_dummy, &j_nr_switches[1]);

	close(j_context_fd);

	return 0;
}


/************************************************************************/
/* Initialize a file that will be used by mmap.				*/
/************************************************************************/
char *
initfile(int fd, long long filebytes,int flag,int prot, int reclen)
{
	 char *pa;
	 int mflags=0;
	 long long x;
	 char *tmp,*stmp;
	 int file_flags;
	 long long recs;
	 long long i;
	 int dflag = 0;

	 if(flag)
	 {

	 	/* 
		  * Allocate a temporary buffer to meet any alignment 
		  * contraints of any method.
		  */
		 tmp=(char *)malloc((size_t)reclen * 2);
		 stmp=tmp;
		 /* 
		  * Align to a reclen boundary.
		  */
		 tmp = (char *)((((long)tmp + (long)reclen))& ~(((long)reclen-1)));
		/* 
		 * Special case.. Open O_DIRECT, and going to be mmap() 
		 * Under Linux, one can not create a sparse file using 
		 * a file that is opened with O_DIRECT 
		 */
	 	file_flags=fcntl(fd,F_GETFL);

		dflag = O_DIRECT;

		{
			/* Save time, just seek out and touch at the end */
		 	lseek(fd,(filebytes-reclen),SEEK_SET);
			x=write(fd,tmp,(size_t)reclen);
			if(x < 1)
			{
				printf("Unable to write file\n");
				//exit(181);
				setState(ERROR, "Unable to write file");
				return NULL;
			}
		}
	 	free(stmp);
	 	lseek(fd,0,SEEK_SET);
	 }

	if((prot & PROT_WRITE)==PROT_WRITE)
		mflags=MAP_FILE|MAP_SHARED;
	else
		mflags=MAP_FILE|MAP_PRIVATE;

	 pa = (char *)mmap( ((char *)0),filebytes, prot, 
	 		mflags, fd, 0);

	if(pa == (char *)-1)
	{
		printf("Mapping failed, errno %d\n",errno);
		//exit(166);
		setState(ERROR, "Mapping failed");
		return NULL;
	}

	return(pa);
}

/************************************************************************/
/* Release the mmap area.						*/
/************************************************************************/
void
mmap_end( char *buffer, long long size)
{
	if(munmap(buffer,(size_t)size)<0)
		printf("munmap failed.\n");	
}

void wait_thread_status(int thread_num, thread_status_t stat, pthread_cond_t* cond)
{
	int ret = 0;
	int i;
	int wait = 0;

//	printf("%s, %d, %d start\n", __func__, thread_num, stat);
	
	ret = pthread_mutex_lock(&thread_lock);
	if(ret < 0)
	{
		perror("pthread_mutex_lock failed");
		//exit(EXIT_FAILURE);
		setState(ERROR, "pthread_mutex_lock failed");
		return;
	}
	
	while(1)
	{
		
		if(thread_num < 0)
		{
			for(i = 0; i < num_threads; i++)
			{
				if(thread_status[i] != stat)
				{
					wait = 1;
					//printf("thread[%d]:%d\n", i, thread_status[i]);
				}
			}

			if(wait)
				wait = 0;
			else
				break;			
		}
		else
		{
			if(thread_status[thread_num] == stat)
				break;
		}

		ret = pthread_cond_wait(cond, &thread_lock);
		if(ret < 0)
		{
			perror("pthread_cond_wait failed");
			//exit(EXIT_FAILURE);
			setState(ERROR, "pthread_cond_wait failed");
			return;
		}
	}
	
	ret = pthread_mutex_unlock(&thread_lock);
	if(ret < 0)
	{
		perror("pthread_mutex_unlock failed");
		//exit(EXIT_FAILURE);
		setState(ERROR, "pthread_mutex_unlock failed");
		return;
	}

//	printf("%s, %d, %d end\n", __func__, thread_num, stat);

	return;
}

void signal_thread_status(int thread_num, thread_status_t stat, pthread_cond_t* cond)
{
	int ret = 0;
	int i;

//	printf("%s, %d, %d start\n", __func__, thread_num, stat);
	
	ret = pthread_mutex_lock(&thread_lock);
	if(ret < 0)
	{
		perror("pthread_mutex_lock failed");
		//exit(EXIT_FAILURE);
		setState(ERROR,"pthread_mutex_lock failed" );
		return;
	}

	if(thread_num < 0)
	{
		for(i = 0; i < num_threads; i++)
		{
			thread_status[i] = stat;
		}
		ret = pthread_cond_broadcast(cond);			
		if(ret < 0)
		{
			perror("pthread_cond_broadcast failed");
			//exit(EXIT_FAILURE);
			setState(ERROR, "pthread_cond_broadcast failed");
			return;
		}		
	}
	else
	{		
		thread_status[thread_num] = stat;
		ret = pthread_cond_signal(cond);	
		if(ret < 0)
		{
			perror("pthread_cond_signal failed");
			//exit(EXIT_FAILURE);
			setState(ERROR, "pthread_cond_signal failed");
			return;
		}		
	}

	ret = pthread_mutex_unlock(&thread_lock);
	if(ret < 0)
	{
		perror("pthread_mutex_unlock failed");
		//exit(EXIT_FAILURE);
		setState(ERROR, "pthread_mutex_unlock failed");
		return;
	}		

//	printf("%s, %d, %d end\n", __func__, thread_num, stat);

	return;
}

#ifdef ANDROID_APP
void show_progress(int pro)
{
	progress = pro;
}
#else
void show_progress(int pro)
{
	static int old_pro = -1;

	if(b_quiet) return;

	if(old_pro == pro) return;

	old_pro = pro;
	printf("%02d%c\r", pro, '%');
	fflush(stdout);
}
#endif

void show_progress_IOPS(int pro, int IOC)
{
	static int old_pro = -1;
	static int old_IOC = -1;

	if(b_quiet) return;

	if(old_pro == pro || old_IOC == IOC) return;
	//if(old_pro == pro ) return;

	old_pro = pro;
        old_IOC = IOC;
	if (g_access == MODE_RND_WRITE || g_access == MODE_RND_READ)
		printf("%02d%c\t\t%d\t%s\r", pro, '%', IOC, "IOPS");
	else if (g_access == MODE_WRITE || g_access == MODE_READ)
		printf("%02d%c\t\t%d\t%s\r", pro, '%', IOC, "KB/sec");

	fflush(stdout);
}

void drop_caches(void)
{
#ifndef ANDROID_APP
//	if(system("sysctl -w vm.drop_caches=3") < 0)
	if(system("echo 3 > /proc/sys/vm/drop_caches") < 0)
	{
		printf("fail to drop caches\n");
		//exit(1);
		setState(ERROR, "fail to drop caches");
		return;
	}
#endif
}

int init_file(char* filename, long long size)
{
	char* buf;
	int ret = 0;
	int fd;
	long long i;
	int rec_len = 512*1024;
	int num_rec = size/rec_len;
	int rest_size = size - (rec_len*num_rec);
	int open_flags = O_RDWR | O_CREAT;

	//printf("%s\n", __func__);
	//printf("size : %d\n", (int)size);
	//printf("num_rec : %d\n", num_rec);
	//printf("rest_size : %d\n", rest_size);

#ifdef ANDROID_APP
	if(g_access == MODE_RND_WRITE)
	{
		open_flags |= O_DIRECT;	
	}
#endif

	fd = open(filename, open_flags, 0766);	
	if(fd < 0)
	{
		printf("%s Open failed %d\n", filename, ret);
		//exit(ret);
		setState(ERROR, "Open failed");
		return -1;
	}

	lseek(fd, 0, SEEK_END);

	buf = malloc(512*1024);
	memset(buf, 0xcafe, 512*1024);

	for(i=0; i<num_rec; i++)
	{		
		if(write(fd, buf, rec_len)<0)
		{
			printf("\nFile write error!!! [no: %d, pos: %lu]\n", errno, lseek(fd, 0, SEEK_CUR)/1024 );
			//exit(1);
			setState(ERROR, "File write error");
			return -1;
		}

		show_progress(100*i/num_rec);
	}	

	if(rest_size)
	{
		if(write(fd, buf, rest_size)<0)
		{
			printf("\nFile write error!!! [no: %d, pos: %lu]\n", errno, lseek(fd, 0, SEEK_CUR)/1024 );
			//exit(1);
			setState(ERROR, "File write error");
			return -1;
		}
	}

	fsync(fd);

	close(fd);

	show_progress(100);

	//printf("\ninit end\n");
	
	free(buf);

	return 0;
}

int thread_main(void* arg)
{
	char* org_buf;
	char* buf;
	int ret = 0;
	int fd;

	unsigned long long init[4]={0x12345ULL, 0x23456ULL, 0x34567ULL, 0x45678ULL};
	unsigned long long length=4;

	long long *recnum= 0;
	long long *tmp_recnum= 0;
	long long i;
	unsigned long long big_rand;
	long long offset;

	char* wmaddr;
	int thread_num = *((int*)arg);
	char filename[128] = {0, };
	int block_open = 0;
	int page_size;
	int open_flags = 0;

	long long IO_Latency;      // start time for measuring IO latency 
	long long Delta_Latency;   // Delta time current_time - IO_Latency
	long long second_hand = 0;     // cumulative Delta_Latency to measure IOPS
	long long IO_Count = 0;              // IO counts in a second
	if(Latency_state==1) fprintf(Latency_fp,"\n#In %d IO mode and %d sync mode\n",g_access, g_sync); // latency header
	if(print_IOPS==1) fprintf(pIOPS_fp,"\n#In %d IO mode and %d sync mode\n",g_access, g_sync); // IOPS print header

	struct stat sb;

	if(num_threads == 1)
	{
		//get_path(getpid());
		storage_count = 0;
		get_path(getpid(), syscall(__NR_gettid));
	}


	//printf("thread start\n");

	if(strncmp(pathname, "/dev", 4) == 0)
	{
		block_open = 1;
	}

	if(block_open == 1)
	{
		sprintf(filename, "%s", pathname);
		printf("open block device : %s\n", filename);		
	}
	else
	{
		sprintf(filename, "%s/test.dat%d", pathname, thread_num);
		//printf("open file : %s\n", filename);
	}

	init_by_array64(init, length);

	recnum = (long long *)malloc(sizeof(*recnum)*numrecs64);
	tmp_recnum = (long long *)malloc(sizeof(*recnum)*numrecs64);

	if(tmp_recnum == NULL || recnum == NULL){
		fprintf(stderr,"Random uniqueness fallback.\n");
	}
	else{
		long long n_overlap_entries = numrecs64 * overlap_ratio / 100;
		long long n_other_entries = numrecs64 - n_overlap_entries;

		/* pre-compute random sequence based on 
		Fischer-Yates (Knuth) card shuffle */

		// initializing the array of random numbers
		if(n_overlap_entries == 0){
			for(i = 0; i < numrecs64; i++){
				recnum[i] = i;
			}
		}
		else{
			for(i = 0; i < numrecs64; i++){ // initialization
				tmp_recnum[i] = i;
			}
			for(i = 0; i < numrecs64; i++) { // shuffling the array
				long long tmp;

				big_rand=genrand64_int64();

				big_rand = big_rand%numrecs64;
				tmp = tmp_recnum[i];
				tmp_recnum[i] = tmp_recnum[big_rand];
				tmp_recnum[big_rand] = tmp;
			}
			for(i = 0; i < n_other_entries; i++){ // copy non-overlapped array
				recnum[i] = tmp_recnum[i];
			}
			for(i = 0; i < n_overlap_entries; i++){ // randomly select from array
				big_rand = genrand64_int64();
				big_rand = big_rand%n_other_entries;

				recnum[n_other_entries+i] = tmp_recnum[big_rand];
			}
		}
		
		for(i = 0; i < numrecs64; i++) { // re-shuffle the array
			long long tmp;

			big_rand=genrand64_int64();

			big_rand = big_rand%numrecs64;
			tmp = recnum[i];
			recnum[i] = recnum[big_rand];
			recnum[big_rand] = tmp;
		}
	}

	if(g_access == MODE_WRITE && block_open == 0)
	{
		ret = unlink(filename);
//		if(ret != 0)
//			printf("Unlink %s failed\n", filename);
	}

	if(g_access == MODE_READ || g_access == MODE_RND_READ || g_access == MODE_RND_WRITE)
	{
		stat(filename, &sb);
		//printf("sb.st_size: %d\n", (int)sb.st_size);

		if(sb.st_size < filebytes64)
		{
			init_file(filename, filebytes64 - sb.st_size);
		}
	}

	if(block_open == 1)
		open_flags = O_RDWR | O_CREAT;
	else if(g_sync == OSYNC)
		open_flags = O_RDWR | O_CREAT | O_SYNC;
	else if(g_sync == ODIRECT)
		open_flags = O_RDWR | O_CREAT | O_DIRECT;
	else if(g_sync == SYDI )
		open_flags = O_RDWR | O_CREAT | O_SYNC | O_DIRECT;
	else
		open_flags = O_RDWR | O_CREAT;

	fd = open(filename, open_flags, 0766);	
	if(fd < 0)
	{
		printf("%s Open failed %d\n", filename, ret);
		//exit(ret);
		setState(ERROR, "open failed");
		signal_thread_status(thread_num, READY, &thread_cond1);
		return -1;
	}

	if(g_sync == MMAP || g_sync == MMAP_AS || g_sync == MMAP_S)
	{
		maddr=(char *)initfile(fd,filebytes64,1,PROT_READ|PROT_WRITE, real_reclen);
	}

	/* align buffer to page_size */
	page_size = sysconf(_SC_PAGESIZE);
	org_buf = malloc(real_reclen+page_size);
	buf=(char *)(((long)org_buf+(long)page_size) & (long)~(page_size-1));
 
	memset(buf, 0xcafe, real_reclen);

//	printf("T%d ready\n", thread_num);

	signal_thread_status(thread_num, READY, &thread_cond1);

	wait_thread_status(thread_num, EXEC, &thread_cond2);
//	printf("T%d start\n", thread_num);

	if(num_threads == 1)
	{
		single_get_nr_switches();
		get_con_switches();		
	}

	if(g_access == MODE_WRITE || g_access == MODE_RND_WRITE)
	{
		for(i=0; i<numrecs64; i++)
		{

			if(g_sync == MMAP || g_sync == MMAP_AS || g_sync == MMAP_S)
		 	{
				if(g_access == MODE_RND_WRITE)
				{
					wmaddr = &maddr[recnum[i]*1024*reclen];
				}
				else
				{
					wmaddr = &maddr[i*real_reclen];
				}
				bcopy((long long*)buf,(long long*)wmaddr,(long long)real_reclen);
				if(g_sync == MMAP_AS)
				{
					msync(wmaddr,(size_t)real_reclen,MS_ASYNC);
				}
				else if(g_sync == MMAP_S)
				{
					msync(wmaddr,(size_t)real_reclen,MS_SYNC);
				}
		 	}
			else
		 	{
				if(g_access == MODE_RND_WRITE)
				{
					offset = (long long)recnum[i]*1024*reclen;	 	 
					//printf("%lld ", offset);

					if(lseek(fd, offset, SEEK_SET)==-1)
					{
						printf("lseek error!!!\n");
						//exit(1);
						setState(ERROR, "lseek error");
						signal_thread_status(thread_num, END, &thread_cond3);
						return -1;
					}
				}		
				// get current time for latency
				if(Latency_state==1 || print_IOPS == 1) IO_Latency=get_current_utime(); 

			 	if(write(fd, buf, real_reclen)<0)
			 	{
					printf("\nFile write error!!! [no: %d, pos: %lu]\n", errno, lseek(fd, 0, SEEK_CUR)/1024 );
					//exit(1);
					setState(ERROR, "File write error");
					signal_thread_status(thread_num, END, &thread_cond3);
					return -1;
			 	}
				
				if(g_sync == FSYNC)
				{
					fsync(fd);
				}
				
				if(g_sync == FDATASYNC)
				{
					fdatasync(fd);
				}
				// if we are checking for IO latency or IOPS
				if(Latency_state == 1 || print_IOPS ==1)
				{
					// get Delta time for an IO
					Delta_Latency = (float)((float)get_relative_utime(IO_Latency)); 
					second_hand = second_hand + Delta_Latency ;
					IO_Count++; // increase IO count
					if(Latency_state==1) // measure the time difference
						fprintf(Latency_fp, "%.0f\tusec\n", (float)Delta_Latency);

                                	if(print_IOPS == 1 && second_hand > 1000000
							&& g_access == MODE_RND_WRITE) // check time to print out iops
					{
						fprintf(pIOPS_fp, "%lld IOPS\n", IO_Count);
						show_progress_IOPS(i*100/numrecs64, (int)IO_Count);
						IO_Count = 0; 
						second_hand = 0;
					}
					else if(print_IOPS == 1 && second_hand > 1000000
							&& g_access == MODE_WRITE)
					{
						fprintf(pIOPS_fp, "%lld KB/s\n", IO_Count*i);
						show_progress_IOPS(i*100/numrecs64, (int)IO_Count);
						IO_Count = 0; 
						second_hand = 0;
					}
				}
		 	}
			if(print_IOPS == 0) // IOPS on progress bar
			{
				show_progress(i*100/numrecs64);
			}
		}	

		/* Final sync */
		if(g_sync == MMAP || g_sync == MMAP_AS || g_sync == MMAP_S)
		{
			msync(maddr,(size_t)filebytes64,MS_SYNC);
		}
		else
		{
			fsync(fd);
		}		
	}
	else
	{
		for(i=0; i<numrecs64; i++)
		{

			if(g_sync == MMAP || g_sync == MMAP_AS || g_sync == MMAP_S)
		 	{
				if(g_access == MODE_RND_READ)
				{
					wmaddr = &maddr[recnum[i]*1024*reclen];
				}
				else
				{
					wmaddr = &maddr[i*real_reclen];
				}
				bcopy((long long*)wmaddr,(long long*)buf, (long long)real_reclen);
		 	}
			else
		 	{
				if(g_access == MODE_RND_READ)
				{
					offset = (long long)recnum[i]*1024*reclen;	 	 
					//printf("%lld ", offset);

					if(lseek(fd, offset, SEEK_SET)==-1)
					{
						printf("lseek error!!!\n");
						//exit(1);
						setState(ERROR, "lseek error");
						signal_thread_status(thread_num, END, &thread_cond3);
						return -1;
					}
				}		
				// start measuring latency 
				if(Latency_state==1 || print_IOPS == 1) IO_Latency=get_current_utime(); 
			 	if(read(fd, buf, real_reclen)<=0)
			 	{
					printf("File read error!!!\n");
					//exit(1);
					setState(ERROR, "File read error");
					signal_thread_status(thread_num, END, &thread_cond3);
					return -1;
			 	}
				// if we are checking for IO latency or IOPS
				if(Latency_state == 1 || print_IOPS ==1)
				{
					// get Delta time for an IO
					Delta_Latency = (float)((float)get_relative_utime(IO_Latency)); 
					second_hand = second_hand + Delta_Latency ;
					IO_Count++; // increase IO count
					if(Latency_state==1) // measure the time difference
						fprintf(Latency_fp, "%.0f\tusec\n", (float)Delta_Latency);

                                	if(print_IOPS == 1 && second_hand > 1000000
							&& g_access == MODE_RND_READ) // check time to print out iops
					{
						fprintf(pIOPS_fp, "%lld IOPS\n", IO_Count);
						show_progress_IOPS(i*100/numrecs64, (int)IO_Count);
						IO_Count = 0; 
						second_hand = 0;
					}
					else if (print_IOPS == 1 && second_hand > 1000000
							&& g_access == MODE_READ)
					{
						fprintf(pIOPS_fp, "%lld KB/s\n", IO_Count*i);
						show_progress_IOPS(i*100/numrecs64, (int)IO_Count);
						IO_Count = 0; 
						second_hand = 0;
					}
				}
		 	}
			if(print_IOPS == 0) // IOPS on progress bar
			{
				show_progress(i*100/numrecs64);
			}
		}	
	}

	show_progress(100);

	if(num_threads == 1)
	{
		single_get_nr_switches();
		get_con_switches();		
	}	

//	printf("T%d end\n", thread_num);
	signal_thread_status(thread_num, END, &thread_cond3);

	if(g_sync == MMAP || g_sync == MMAP_AS || g_sync == MMAP_S)
		mmap_end(maddr,(unsigned long long)filebytes64);

	 close(fd);

	 free(org_buf);
 
 	if(recnum)
		free(recnum);

	if(tmp_recnum)
		free(tmp_recnum);

//	printf("thread end\n");

	return 0;

}

int sql_cb(void* data, int ncols, char** values, char** headers)
{
	int i;

	for(i = 0; i < ncols; i++)
	{
		printf("%s=%s\n", headers[i], values[i]);
	}
	
	return 0;
}

#define exec_sql(db, sql, cb)	sqlite3_exec(db, sql, cb, NULL, NULL);

int init_db_for_update(sqlite3* db, char* filename, int trs)
{
	int i;

	printf("%s\n", __func__);
	printf("trs : %d\n", trs);

	for(i = 0; i < trs; i++) 
	{
		exec_sql(db, "INSERT INTO tblMyList(Value) VALUES('"INSERT_STR"');", NULL);
		show_progress(i*100/trs);
	}

	show_progress(100);

	printf("\ninit end\n");
	
	return 0;
}

char* get_journal_mode_string(int journal_mode)
{
	char* ret_str;
	
	switch(journal_mode) {
		case 0:
			ret_str = "DELETE";
			break;
		case 1:
			ret_str = "TRUNCATE";
			break;
		case 2:
			ret_str = "PERSIST";
			break;
		case 3:
			ret_str = "WAL";
			break;
		case 4:
			ret_str = "MEMORY";
			break;
		case 5:
			ret_str = "OFF";
			break;
		case 6:
			ret_str = "PMEM";
			break;
		default:
			ret_str = "TRUNCATE";
			break;		
	}

	return ret_str;
}

int thread_main_db(void* arg)
{

	int thread_num = *((int*)arg);
	char filename[128] = {0, };
	sqlite3 *db;
	int rc;
	int i;
	char sql[1024] = {0, };

	if(num_threads == 1)
	{
		storage_count = 0;
		get_path(getpid(), syscall(__NR_gettid));
	}

//	printf("db thread start\n");

	sprintf(filename, "%s/test.db%d", pathname, thread_num);
//	printf("open db : %s\n", filename);

	rc = sqlite3_open(filename, &db);

	if(SQLITE_OK != rc)
	{
		fprintf(stderr, "rc = %d\n", rc);
		fprintf(stderr, "sqlite3_open error :%s\n", sqlite3_errmsg(db));
		//exit(EXIT_FAILURE);
		setState(ERROR, "sqlite3_open error");
		signal_thread_status(thread_num, READY, &thread_cond1);
		return -1;
	}

	exec_sql(db, "PRAGMA page_size = 4096;", NULL);
	sprintf(sql, "PRAGMA journal_mode=%s;", get_journal_mode_string(db_journal_mode));
	exec_sql(db, sql, NULL);
	sprintf(sql, "PRAGMA synchronous=%d;", db_sync_mode);
	exec_sql(db, sql, NULL);

	if(db_init_show == 0)
	{
		db_init_show = 1;
		printf("-----------------------------------------\n");		
		printf("[DB information]\n");
		printf("-----------------------------------------\n");
		exec_sql(db, "select sqlite_version() AS sqlite_version;", sql_cb);
		exec_sql(db, "PRAGMA page_size;", sql_cb);
		exec_sql(db, "PRAGMA journal_mode;", sql_cb);
		exec_sql(db, "PRAGMA synchronous;", sql_cb);		
	}

	if(db_mode == 0)
	{
		exec_sql(db, "DROP TABLE IF EXISTS tblMyList;", NULL);
	}

	exec_sql(db, "CREATE TABLE IF NOT EXISTS tblMyList (id INTEGER PRIMARY KEY autoincrement, Value TEXT not null, creation_date long);", NULL);

	/* check column count */
	if(db_mode != 0)
	{
		char** result;
		char* errmsg;
		int rows, columns;
		int column_count = 0;
		sprintf(sql, "SELECT count(*) from tblMyList;");
		rc = sqlite3_get_table(db, sql, &result, &rows, &columns, &errmsg);
		column_count = atoi(result[1]);
		//printf("existing column count : %d\n", column_count);
		sqlite3_free_table(result);

		if(column_count < db_transactions)
		{
			init_db_for_update(db, filename, db_transactions - column_count);
		}
	}

	//sqlite3_db_release_memory(db);
	
	signal_thread_status(thread_num, READY, &thread_cond1);
	wait_thread_status(thread_num, EXEC, &thread_cond2);

	if(num_threads == 1)
	{
		single_get_nr_switches();
		get_con_switches();		
	}

	for(i = 0; i < db_transactions; i++) 
	{
		if(db_mode == 0)
		{
			exec_sql(db, "INSERT INTO tblMyList(Value) VALUES('"INSERT_STR"');", NULL);
		}
		else if(db_mode == 1)
		{
			srand(i);
			sprintf(sql, "UPDATE tblMyList SET Value = '%s' WHERE id = %d;", UPDATE_STR, rand()%db_transactions + 1);
			exec_sql(db, sql, NULL);
		}
		else if(db_mode == 2)
		{
			sprintf(sql, "DELETE FROM tblMyList WHERE id=%d;", i);
			exec_sql(db, sql, NULL);
		}
		else
		{
			fprintf(stderr, "invaild db operation mode %d\n", db_mode);
			setState(ERROR, "invaild db operation mode");
			signal_thread_status(thread_num, END, &thread_cond3);
			return -1;
		}

		if(db_interval)
		{
			sleep(db_interval);
		}
		
		show_progress(i*100/db_transactions);
	}

	/* Forced checkpointing for WAL mode */
	if(db_journal_mode == 3)
	{
		sqlite3_wal_checkpoint(db, NULL);
	}

	show_progress(100);

	if(num_threads == 1)
	{
		single_get_nr_switches();
		get_con_switches();		
	}	

	signal_thread_status(thread_num, END, &thread_cond3);

	if(db_mode == 2)
	{
		exec_sql(db, "DROP TABLE tblMyList;", NULL);
	}

	rc = sqlite3_close(db);

	if(SQLITE_OK != rc)
	{
		fprintf(stderr, "rc = %d\n", rc);
		fprintf(stderr, "sqlite3_close error :%s\n", sqlite3_errmsg(db));
		return -1;
	}	

	if(db_mode == 2)
	{
		unlink(filename);
	}

//	printf("db thread end\n");

	return 0;

}

int readline(FILE *f, char *buffer, size_t len)
{
   char c; 
   int i;

   memset(buffer, 0, len);

   for (i = 0; i < len; i++)
   {   
      int c = fgetc(f); 

      if (!feof(f)) 
      {   
         if (c == '\r')
            buffer[i] = 0;
         else if (c == '\n')
         {   
            buffer[i] = 0;

            return i+1;
         }   
         else
            buffer[i] = c; 
      }   
      else
      {   
         //fprintf(stderr, "read_line(): recv returned %d\n", c);
         return -1; 
      }   
   }   

   return -1; 
}

long long get_current_utime(void)
{
	struct timeval current;
	
	gettimeofday(&current,NULL);
	
	return (current.tv_sec*1000000 + current.tv_usec);	
}

long long get_relative_utime(long long start)
{
	struct timeval current;
	
	gettimeofday(&current,NULL);
	
	return (current.tv_sec*1000000 + current.tv_usec - start);		
}

int get_new_fd(int fd_org)
{
	int i;
	int fd_new = -1;

	//printf("org:%d, index:%d\n", fd_org, gScriptFdConv.index);

	for(i = 0; i < gScriptFdConv.index; i++)
	{
		if(gScriptFdConv.fd_org[i] == fd_org)
		{
			fd_new = gScriptFdConv.fd_new[i];
			break;
		}
	}
	//printf("new:%d\n", fd_new);
	return fd_new;
	
}

int open_num = 0;

int do_script(struct script_entry* se, struct script_thread_time* st)
{
	int ret;
	char replay_pathname[256];
	int flags;
	int i;
	int fd_ret;
	long long io_time_start;
	long long io_time = -1;
	
	//printf("thread[%d] %s\n", se->thread_num, se->cmd);
	if( strncmp(se->cmd, "open", 4) == 0)
	{
		int fd_org = atoi(se->args[2]);
		
		//printf("open %s, %s, %d\n", se->args[0], se->args[1], fd_org);
		if(se->args[0][0] == '"')
		{
			strcpy(replay_pathname, &se->args[0][1]);
			replay_pathname[strlen(replay_pathname)-1]='\0';
		}
		else
		{
			//sprintf(pathname, "./data/temp_%d_%d.dat", se->thread_num, open_num++);	
			//sprintf(pathname, "/data2/test/temp_%d_%d.dat", se->thread_num, open_num++);	
			sprintf(replay_pathname, "%s/temp_%d_%d.dat", pathname, se->thread_num, open_num++);	
		}

		if( strncmp(se->args[1], "O_RDONLY", 8) == 0)
		{
			flags = O_RDONLY;
		}
		else
		{
			flags = O_RDWR|O_CREAT|O_TRUNC;
		}

		io_time_start = get_current_utime();
		fd_ret = open(replay_pathname, flags, 0777);
		io_time = get_relative_utime(io_time_start);
		SCRIPT_PRINT("open %s --> %d\n", replay_pathname, fd_ret);
		if(fd_ret > 0)
		{
			ret = pthread_mutex_lock(&fd_lock);
			if(ret < 0)
			{
				perror("pthread_mutex_lock failed");
				setState(ERROR, "pthread_mutex_lock failed");
				return -1;
			}
			//printf("open org:%d, new:%d, index:%d\n", fd_org, fd_ret, gScriptFdConv.index);
			
			gScriptFdConv.fd_org[gScriptFdConv.index] = fd_org;
			gScriptFdConv.fd_new[gScriptFdConv.index] = fd_ret;
			gScriptFdConv.index++;
			
			ret = pthread_mutex_unlock(&fd_lock);
			if(ret < 0)
			{
				perror("pthread_mutex_unlock failed");
				setState(ERROR, "pthread_mutex_unlock failed");
				return -1;
			}
		}
	}
	else if( strncmp(se->cmd, "close", 5) == 0)
	{
		int fd_org = atoi(se->args[0]);
		int fd_new = 0;
		
		//printf("close %d, %d\n", fd_org, atoi(se->args[1]));
		
		ret = pthread_mutex_lock(&fd_lock);
		if(ret < 0)
		{
			perror("pthread_mutex_lock failed");
			setState(ERROR, "pthread_mutex_lock failed");
			return -1;
		}

		for(i = 0; i < gScriptFdConv.index; i++)
		{
			if(gScriptFdConv.fd_org[i] == fd_org)
			{
				//printf("close matched org:%d, new:%d\n", fd_org, gScriptFdConv.fd_new[i]);
				fd_new = gScriptFdConv.fd_new[i];
				gScriptFdConv.fd_org[i] = 0;
				gScriptFdConv.fd_new[i] = 0;
				break;
			}
		}
		ret = pthread_mutex_unlock(&fd_lock);
		if(ret < 0)
		{
			perror("pthread_mutex_unlock failed");
			setState(ERROR, "pthread_mutex_unlock failed");
			return -1;
		}

		if(fd_new)
		{
			io_time_start = get_current_utime();
			ret = close(fd_new);
			io_time = get_relative_utime(io_time_start);
			SCRIPT_PRINT("close %d --> %d\n", fd_new, ret);
		}
		
	}
	else if( strncmp(se->cmd, "write", 5) == 0)
	{
		int fd_new = get_new_fd(atoi(se->args[0]));

		if(fd_new > 0)
		{
			io_time_start = get_current_utime();
			ret = write(fd_new, script_write_buf, atoi(se->args[1]));
			io_time = get_relative_utime(io_time_start);
			SCRIPT_PRINT("write %d, %d --> %d\n", fd_new, atoi(se->args[1]), ret);
			if(ret > 0)
			{
				st->write_size += ret;
			}
		}
	}
	else if( strncmp(se->cmd, "pwrite", 6) == 0)
	{
		int fd_new = get_new_fd(atoi(se->args[0]));

		if(fd_new > 0)
		{
			io_time_start = get_current_utime();
			ret = pwrite(fd_new, script_write_buf, atoi(se->args[2]), atoi(se->args[1]));
			io_time = get_relative_utime(io_time_start);
			SCRIPT_PRINT("pwrite %d, %d (at %d) --> %d\n", fd_new, atoi(se->args[2]), atoi(se->args[1]), ret);
			if(ret > 0)
			{
				st->write_size += ret;
			}
		}
	}
	else if( strncmp(se->cmd, "read", 4) == 0)
	{
		int fd_new = get_new_fd(atoi(se->args[0]));

		if(fd_new > 0)
		{
			io_time_start = get_current_utime();
			ret = read(fd_new, script_read_buf, atoi(se->args[1]));
			io_time = get_relative_utime(io_time_start);
			SCRIPT_PRINT("read %d, %d --> %d\n", fd_new, atoi(se->args[1]), ret);
			if(ret > 0)
			{
				st->read_size += ret;
			}
		}
	}
	else if( strncmp(se->cmd, "pread", 5) == 0)
	{
		int fd_new = get_new_fd(atoi(se->args[0]));

		if(fd_new > 0)
		{
			io_time_start = get_current_utime();
			ret = pread(fd_new, script_read_buf, atoi(se->args[2]), atoi(se->args[1]));
			io_time = get_relative_utime(io_time_start);
			SCRIPT_PRINT("pread %d, %d (at %d) --> %d\n", fd_new, atoi(se->args[2]), atoi(se->args[1]), ret);
			if(ret > 0)
			{
				st->read_size += ret;
			}
		}
	}
	else if( strncmp(se->cmd, "fsync", 5) == 0)
	{
		int fd_new = get_new_fd(atoi(se->args[0]));

		if(fd_new > 0)
		{
			io_time_start = get_current_utime();
			ret = fsync(fd_new);
			io_time = get_relative_utime(io_time_start);
			SCRIPT_PRINT("fsync %d --> %d\n", fd_new, ret);
		}
	}
	else if( strncmp(se->cmd, "fdatasync", 9) == 0)
	{
		int fd_new = get_new_fd(atoi(se->args[0]));

		if(fd_new > 0)
		{
			io_time_start = get_current_utime();
			ret = fdatasync(fd_new);
			io_time = get_relative_utime(io_time_start);
			SCRIPT_PRINT("fdatasync %d --> %d\n", fd_new, ret);
		}
	}
	else if( strncmp(se->cmd, "access", 6) == 0)
	{
		strcpy(replay_pathname, &se->args[0][1]);
		replay_pathname[strlen(replay_pathname)-1]='\0';

		io_time_start = get_current_utime();
		ret = access(replay_pathname,  0777);
		io_time = get_relative_utime(io_time_start);
		SCRIPT_PRINT("access %s --> %d\n", replay_pathname, ret);
	}
	else if( strncmp(se->cmd, "stat", 4) == 0)
	{
		struct stat64 stat_buf;
		strcpy(replay_pathname, &se->args[0][1]);
		replay_pathname[strlen(replay_pathname)-1]='\0';

		io_time_start = get_current_utime();
		ret = stat64(replay_pathname,  &stat_buf);
		io_time = get_relative_utime(io_time_start);
		SCRIPT_PRINT("stat64 %s --> %d\n", replay_pathname, ret);
	}
	else if( strncmp(se->cmd, "lstat", 5) == 0)
	{
		struct stat64 stat_buf;
		strcpy(replay_pathname, &se->args[0][1]);
		replay_pathname[strlen(replay_pathname)-1]='\0';

		io_time_start = get_current_utime();
		ret = lstat64(replay_pathname,  &stat_buf);
		io_time = get_relative_utime(io_time_start);
		SCRIPT_PRINT("lstat64 %s --> %d\n", replay_pathname, ret);
	}
	else if( strncmp(se->cmd, "fstat", 5) == 0)
	{
		struct stat64 stat_buf;
		int fd_new = get_new_fd(atoi(se->args[0]));

		if(fd_new > 0)
		{
			io_time_start = get_current_utime();
			ret = fstat64(fd_new,  &stat_buf);
			io_time = get_relative_utime(io_time_start);
			SCRIPT_PRINT("fstat64 %d --> %d\n", fd_new, ret);
		}
	}
	else if( strncmp(se->cmd, "unlink", 6) == 0)
	{
		strcpy(replay_pathname, &se->args[0][1]);
		replay_pathname[strlen(replay_pathname)-1]='\0';

		io_time_start = get_current_utime();
		ret = unlink(replay_pathname);
		io_time = get_relative_utime(io_time_start);
		SCRIPT_PRINT("unlink %s --> %d\n", replay_pathname, ret);
	}

	return io_time;
}

int script_thread_main(void* arg)
{
	struct script_thread_info* thread_info = (struct script_thread_info*)arg;
	int thread_num = thread_info->thread_num;
	int cmd_cnt = 0;
	int i;
	long long io_time;

	gScriptThreadTime[thread_num].io_time = 0;
	gScriptThreadTime[thread_num].write_size = 0;
	gScriptThreadTime[thread_num].read_size = 0;
	//printf("thread[%d] started at %lld, org %lld\n", thread_num, get_relative_utime(time_start), gScriptThreadTime[thread_num].start);

	for(i = 0; i < thread_info->line_count; i++)
	{
		if(gScriptEntry[i].thread_num == thread_num)
		{
			long long time_diff = gScriptEntry[i].time - get_relative_utime(time_start);
			if(time_diff > 1)
			{
				//printf("sleep %lld\n", time_diff);
				usleep(time_diff-1);
			}
			//printf("%lld\n", get_relative_utime(time_start) - gScriptEntry[i].time);
			io_time = do_script(&gScriptEntry[i], &gScriptThreadTime[thread_num]);
			usleep(0);
			if(io_time >= 0)
			{
				gScriptThreadTime[thread_num].io_time += io_time;
				gScriptThreadTime[thread_num].io_count++;
				//printf("thread[%d] %s\n", thread_num, gScriptEntry[i].cmd);
			}
		}
	}

	gScriptThreadTime[thread_num].end = 1;
	//printf("thread[%d] ended at %lld, org %lld\n", thread_num, get_relative_utime(time_start), gScriptThreadTime[thread_num].end);
	return 0;
}

/*
 * main function for Replay script
 */
int replay_script(void)
{
	FILE* fp;
	char line_buf[1024];
	int ret = 0;
	int line_count = 0;
	int i, j;
	int old_thread_num = -1;
	int thread_index = -1;
	long long time_current;
	pthread_t*	thread_id;
	struct script_thread_info* thread_info;
	int thread_start_cnt = 0;
	void* res;
	int open_count = 0;
	int max_write_size = 0;
	int max_read_size = 0;
	long long total_io_time;
	int total_io_count;
	long long real_end_time = 0;
	int total_read;
	int total_write;
	
	printf("%s start\n", __func__);
	printf("Write target : %s\n", pathname);

	/* 
	* Open script file (output of MobiGen script)
	*/
	fp = fopen(script_path, "r");
	if(fp == NULL)
	{
		printf("%s Open failed %p\n", script_path, fp);
		setState(ERROR, "open failed");
		return -1;
	}

	/*
	* Scan script file and count lines
	*/
	do {
		ret = readline(fp, line_buf, sizeof(line_buf));
		if(ret > 0) {
			line_count++;		
		}
	}while(ret > 0);
	printf("line count : %d\n", line_count);

	/* 
	* Allocate memory for all script entries using line_count
	*/
	gScriptEntry = (struct script_entry*)malloc(line_count*sizeof(struct script_entry));
	if(gScriptEntry == NULL)
	{
		printf("gScriptEntry malloc failed\n");
		setState(ERROR, "malloc failed");
		return -1;
	}

	/*
	* Parse and save items from each script lines
	*/
	fseek(fp, 0, SEEK_SET);
	for(i = 0; i < line_count; i++)
	{
		int args_num = 0;
		char* ptr;
		memset(line_buf, 0, sizeof(line_buf));
		ret = readline(fp, line_buf, sizeof(line_buf));
		
		ptr = strtok(line_buf, " ");
		gScriptEntry[i].thread_num = atoi(ptr);

		ptr = strtok( NULL, " ");
		gScriptEntry[i].time = atoll(ptr);
		
		ptr = strtok( NULL, " ");
		gScriptEntry[i].cmd = (char*)malloc(strlen(ptr)+1);
		strcpy(gScriptEntry[i].cmd, ptr);
				
		while( ptr = strtok( NULL, " "))
		{
			gScriptEntry[i].args[args_num] = (char*)malloc(strlen(ptr)+1);
			strcpy(gScriptEntry[i].args[args_num], ptr);
			args_num++;
			if(args_num == 3) break;
		}
		gScriptEntry[i].arg_num = args_num;

	}

	/*
	* Close script file
	*/
	fclose(fp);

	/*
	* Get additional information from script entry.
	* : number of thread, start/end time of threads, open count, 
	*  MAX R/W size and original execution time
	*/

	/* Number of thread */
	script_thread_num = gScriptEntry[line_count-1].thread_num+1;
	printf("script_thread_num: %d\n", script_thread_num);
	
	gScriptThreadTime = (struct script_thread_time*)malloc(script_thread_num * 
		sizeof(struct script_thread_time));
	if(gScriptThreadTime == NULL) {
		printf("gScriptThreadTime malloc failed\n");
		setState(ERROR, "malloc failed");
		return -1;
	}

	/* memset for gScriptThreadTime */
	memset(gScriptThreadTime, 0x0, script_thread_num*sizeof(struct script_thread_time));

	for(i = 0; i < line_count; i++) {
		/* Start/end time of each thread */
		if(old_thread_num != gScriptEntry[i].thread_num) {
			thread_index++;
			old_thread_num = gScriptEntry[i].thread_num;
			gScriptThreadTime[thread_index].start = gScriptEntry[i].time;
			gScriptThreadTime[thread_index].thread_num = gScriptEntry[i].thread_num;
		} else {
			gScriptThreadTime[thread_index].end = gScriptEntry[i].time;
		}
		
		gScriptThreadTime[thread_index].count++;

		/* Get total count of 'open' system-call */
		if( strncmp(gScriptEntry[i].cmd, "open", 4) == 0)
		{
			open_count++;
		}
		/* Get maximum size of read/write system-call */
		else if( (strncmp(gScriptEntry[i].cmd, "write", 5) == 0))
		{
			int size = atoi(gScriptEntry[i].args[1]);
			if(size > max_write_size)
			{
				max_write_size= size;
			}
		}
		else if( (strncmp(gScriptEntry[i].cmd, "pwrite", 6) == 0))
		{
			int size = atoi(gScriptEntry[i].args[2]);
			if(size > max_write_size)
			{
				max_write_size= size;
			}			
		}	
		else if( (strncmp(gScriptEntry[i].cmd, "read", 5) == 0))
		{
			int size = atoi(gScriptEntry[i].args[1]);
			if(size > max_read_size)
			{
				max_read_size= size;
			}
		}
		else if( (strncmp(gScriptEntry[i].cmd, "pread", 6) == 0))
		{
			int size = atoi(gScriptEntry[i].args[2]);
			if(size > max_read_size)
			{
				max_read_size= size;
			}			
		}	

		/* Get original execution time */
		if(real_end_time < gScriptEntry[i].time)
		{
			real_end_time = gScriptEntry[i].time;
		}
		
	}
	script_thread_num = thread_index+1;	/* real number of threads */
	printf("open count : %d\n", open_count);
	printf("max size : W %d, R %d\n", max_write_size, max_read_size);
	printf("original execution time : %.3f sec\n", (double)real_end_time/1000000);
#if 0
	for(i=0; i <= thread_index; i++)
	{
		printf("%d, %d, %lld, %lld\n", i, gScriptThreadTime[i].count, gScriptThreadTime[i].start, gScriptThreadTime[i].end);
	}
#endif	

	/*
	* Allocate memory for additional informations.
	*/
	thread_id = (pthread_t*)malloc(sizeof(pthread_t)*script_thread_num);
	if(thread_id == NULL)
	{
		printf("thread_id malloc failed\n");
		setState(ERROR, "malloc failed");
		return -1;
	}
	thread_info = (struct script_thread_info*)malloc(sizeof(struct script_thread_info)*script_thread_num);
	if(thread_info == NULL)
	{
		printf("thread_info malloc failed\n");
		setState(ERROR, "malloc failed");
		return -1;
	}

	gScriptFdConv.fd_org= (int*)malloc(sizeof(int)*open_count);
	if(gScriptFdConv.fd_org == NULL)
	{
		printf("gScriptFdConv.fd_org malloc failed\n");
		setState(ERROR, "malloc failed");
		return -1;
	}

	gScriptFdConv.fd_new= (int*)malloc(sizeof(int)*open_count);
	if(gScriptFdConv.fd_new == NULL)
	{
		printf("gScriptFdConv.fd_new malloc failed\n");
		setState(ERROR, "malloc failed");
		return -1;
	}

	script_write_buf = (char*)malloc(max_write_size);
	script_read_buf = (char*)malloc(max_read_size);
	memset(script_write_buf, 0xcafe,max_write_size); 

	/* 
	* Start run threads
	*/

	drop_caches();
	
	time_start = get_current_utime();
	
	while(1) {
		time_current = get_relative_utime(time_start);
		for(i = 0; i < 	script_thread_num; i++)
		{
			if(gScriptThreadTime[i].started == 0)
			{
				/* If the start time of a thread reached, create the thread. */
				if(gScriptThreadTime[i].start <= (time_current+1000000))	/* 1sec margin */
				{
					thread_info[i].thread_num = gScriptThreadTime[i].thread_num;
					thread_info[i].line_count = line_count;
					thread_info[i].open_count = open_count;
					ret = pthread_create((pthread_t *)&thread_id[i], NULL, (void*)script_thread_main, &thread_info[i]);
					pthread_detach(thread_id[i]);
					gScriptThreadTime[i].started = 1;
					thread_start_cnt++;
				}
			}
		}
		if(thread_start_cnt >= script_thread_num) {
			break;
		}
	}

	/* 
	* Wait until all threads complete its run
	*/
	while(1)
	{
		int not_end = 0;
		
		for(i = 0; i < script_thread_num; i++)
		{
			if(gScriptThreadTime[gScriptThreadTime[i].thread_num].end != 1)
			{
				not_end = 1;
			}
		}
		if(not_end == 0)
		{
			break;
		}
	}

	/*
	* Compute total IO time and total IO count.
	*/
	total_io_time = 0;
	total_io_count = 0;
	total_read = 0;
	total_write = 0;
	for(i = 0; i < script_thread_num; i++)
	{
		//printf("thread[%d] %lld\n", i, gScriptThreadTime[gScriptThreadTime[i].thread_num].io_time);
		total_io_time += gScriptThreadTime[gScriptThreadTime[i].thread_num].io_time;
		total_io_count += gScriptThreadTime[gScriptThreadTime[i].thread_num].io_count;
		total_write += gScriptThreadTime[gScriptThreadTime[i].thread_num].write_size;
		total_read += gScriptThreadTime[gScriptThreadTime[i].thread_num].read_size;
	}

	//printf("join start \n");
#if 0	
	/* Join threads */
	for(i = 0; i < script_thread_num; i++)
	{
		ret = pthread_join(thread_id[i], &res);
		if(ret < 0)
		{
			perror("pthread_join failed");
			setState(ERROR, "pthread_join failed");
			return -1;
		}
		//free(res);
	}
#endif	
	printf("%s end\n", __func__);
	printf("Total IO time : %.3f sec (%lld usec)\n", (double)total_io_time/1000000, total_io_time);
	printf("Total IO count : %d \n", total_io_count);
	printf("Total Write: %d bytes, Read: %d bytes\n", total_write, total_read);

	/* 
	* Free all memories 
	*/
	free(script_write_buf);
	free(script_read_buf);

	free(gScriptFdConv.fd_org);
	free(gScriptFdConv.fd_new);
	free(thread_id);
	free(thread_info);

	for(i = 0; i < line_count; i++)
	{	
		free(gScriptEntry[i].cmd);
		for(j = 0; j < gScriptEntry[i].arg_num ; j++)
		{
			free(gScriptEntry[i].args[j]);
		}
	}	

	free(gScriptEntry);
	free(gScriptThreadTime);

	return 0;
}

char *help[] = {
" Mobibench "VERSION_NUM,
" ",
"    Usage: mobibench [-p pathname] [-f file_size_Kb] [-r record_size_Kb] [-a access_mode] [-h]",
"                     [-y sync_mode] [-t thread_num] [-d db_mode] [-n db_transcations]",
"                     [-j SQLite_journalmode] [-s SQLite_syncmode] [-g replay_script] [-q]",
"                     [-L IO_Latency_file] [-k IOPS_FILE]  [-v overlap_ratio_%]",
" ",
"           -p  set path name (default=./mobibench)",
"           -f  set file size in KBytes (default=1024)",
"           -r  set record size in KBytes (default=4)",
"           -a  set access mode (0=Write, 1=Random Write, 2=Read, 3=Random Read) (default=0)",
"           -y  set sync mode (0=Normal, 1=O_SYNC, 2=fsync, 3=O_DIRECT, 4=Sync+direct,",
"                              5=mmap, 6=mmap+MS_ASYNC, 7=mmap+MS_SYNC 8=fdatasync) (default=0)",
"           -t  set number of thread for test (default=1)",
"           -d  enable DB test mode (0=insert, 1=update, 2=delete)",
"           -n  set number of DB transaction (default=10)",
"           -j  set SQLite journal mode (0=DELETE, 1=TRUNCATE, 2=PERSIST, 3=WAL, 4=MEMORY, ",
"                                        5=OFF, 6=PMEM) (default=1)",
"           -s  set SQLite synchronous mode (0=OFF, 1=NORMAL, 2=FULL) (default=2)",
"           -g  set replay script (output of MobiGen)",
"           -q  do not display progress(%) message",
"           -L  Make record on latency of each IO to a file (default=IO_latency.txt)",
"           -k  Print out IOPS of the file test (default=IO_latency.txt)",
"	    -v  set overlap ratio(%) of random numbers for",
"					random IO workload (default=0%)",
""
};

void show_help()
{
	int i;
	for(i=0; strlen(help[i]); i++)
    {
		printf("%s\n", help[i]);
    }
	return;	
}

extern char *optarg;
#define USAGE  "\tMobibench "VERSION_NUM"\n\n\tUsage: For usage information type mobibench -h \n\n"

int main( int argc, char **argv)
{
	int ret = 0;
	int count;
	struct timeval T1, T2;
	int i = 0;
	char* maddr;
	pthread_t	thread_id[MAX_THREADS];
	void* res;
	int thread_info[MAX_THREADS];	
	int cret;

#if 1
	if(argc <=1){
		printf(USAGE);
		return 0;
	}
#endif

    init_fs();

	/* set default */
	strcpy(pathname, "./mobibench");
	kilo64 = 1024;	/* 1MB */
	reclen = 4;		/* 4KB */
	g_access = 0;	/* Write */
	g_sync = 0;		/* Normal */
	num_threads = 1;	
	db_test_enable = 0;	/* DB off */
	db_mode = 0;	/* insert */
	db_transactions = 10;
	db_journal_mode = 1; /* TRUNCATE */
	db_sync_mode = 2; /* FULL */
	db_init_show = 0;
	b_quiet = 0;
	b_replay_script = 0;
	db_interval= 0;
        strcpy(REPORT_Latency, "./IO_Latency.txt");
        strcpy(REPORT_pIOPS, "./IOPS.txt");
	clearState();

	optind = 1;

	overlap_ratio = 0;

	while((cret = getopt(argc,argv,"p:f:r:a:y:t:d:n:j:s:g:i:hqL:k:v:")) != EOF){
		switch(cret){
			case 'p':
				strcpy(pathname, optarg);
				break;
			case 'f':
				kilo64 = (long long)atoi(optarg);
				break;
			case 'r':
				reclen = (long long)atoi(optarg);
				break;
			case 'a':
				g_access = atoi(optarg);
				break;
			case 'y':
				g_sync = atoi(optarg);
				break;
			case 't':
				num_threads = atoi(optarg);
				break;
			case 'd':
				db_test_enable = 1;
				db_mode = atoi(optarg);
				break;
			case 'n':
				db_transactions = atoi(optarg);
				break;
			case 'j':
				db_journal_mode = atoi(optarg);
				break;
			case 's':
				db_sync_mode = atoi(optarg);
				break;
			case 'h':
				show_help();
				return 0;
				break;
			case 'q':
				b_quiet = 1;
				break;
			case 'g':
				script_path=optarg;
				b_replay_script = 1;
				break;
			case 'i':
				db_interval = atoi(optarg);
				break;
			case 'L':
				strcpy(REPORT_Latency, optarg);
				Latency_state=1;
				break;
			case 'k':
				strcpy(REPORT_pIOPS, optarg);
				print_IOPS=1;
				break;
			case 'v':
				overlap_ratio = atoi(optarg);
				break;
			default:
				return 1;
				break;
		}
	}

	if(b_replay_script == 1)
	{
		DIR* dir;
		dir = opendir(pathname);
		if(dir == NULL)
		{	
			printf("Invalid path name %s\n", pathname);
			setState(ERROR, "path name error");
			goto out;
		}
		
		strcat(pathname, "/mobigen_temp");
		mkdir(pathname, S_IRWXU | S_IRWXG | S_IRWXO);
		replay_script();
		goto out;
	}	

	if(num_threads > MAX_THREADS)
		num_threads = MAX_THREADS;
	else if(num_threads < 1)
		num_threads = 1;
		
	real_reclen = reclen*SIZE_1KB;
  	kilo64 /= num_threads;
	db_transactions /= num_threads;
    numrecs64 = kilo64/reclen;	
	filebytes64 = numrecs64*real_reclen; 
	
	mkdir(pathname, S_IRWXU | S_IRWXG | S_IRWXO);

#ifdef ANDROID_APP
	if(g_access == MODE_READ || g_access == MODE_RND_READ)
	{
		g_sync = 3;		/* 3=O_DIRECT */
	}
#endif

	if(Latency_state==1) // open a file to print latency output
	{
		if( (Latency_fp=fopen(REPORT_Latency,"a")) == 0 )
		{
			printf("Unable to open %s", REPORT_Latency);
			perror("Failed to open a file");		
		}
	}
	
	if(print_IOPS==1) // open a file to print IOPS on every second
	{
		if( (pIOPS_fp=fopen(REPORT_pIOPS,"a")) == 0 )
		{
			printf("Unable to open %s", REPORT_pIOPS);
			perror("Failed to open a file");		
		}
	}
	
	printf("-----------------------------------------\n");
	printf("[mobibench measurement setup]\n");
	printf("-----------------------------------------\n");

	if(db_test_enable == 0)
	{
		printf("File size per thread : %lld KB\n", kilo64);
		printf("IO size : %lld KB\n", reclen);		
		printf("IO count : %lld \n", numrecs64);
		printf("Access Mode %d, Sync Mode %d\n", g_access, g_sync);	
	}
	else
	{
		printf("DB teset mode enabled.\n");
		printf("Operation : %d\n", db_mode);
		printf("Transactions per thread: %d\n", db_transactions);
	}
	printf("# of Threads : %d\n", num_threads);
 
	/* Creating threads */
	for(i = 0; i < num_threads; i++)
	{
		thread_info[i]=i;
		if(db_test_enable == 0)
		{
			ret = pthread_create((pthread_t *)&thread_id[i], NULL, (void*)thread_main, &thread_info[i]);
		}
		else
		{
			ret = pthread_create((pthread_t *)&thread_id[i], NULL, (void*)thread_main_db, &thread_info[i]);
		}
		if(ret < 0)
		{
			perror("pthread_create failed");
			//exit(EXIT_FAILURE);
			setState(ERROR, "pthread_create failed");
			return -1;
		}
		//printf("pthread_create id : %d\n", (int)thread_id[i]);
	}

	setState(READY, NULL);
	
	/* Wait until all threads are ready to perform */
	wait_thread_status(-1, READY, &thread_cond1);

	drop_caches();

	/* Start measuring data for performance */
	gettimeofday(&T1,NULL);
	cpuUsage(START_CPU_CHECK);

	setState(EXEC, NULL);

	/* Send signal to all threads to start */
	signal_thread_status(-1, EXEC, &thread_cond2);

	if(getState() == ERROR)
	{
		goto out;
	}

	/* Wait until all threads done */
	wait_thread_status(-1, END, &thread_cond3);

	if(getState() == ERROR)
	{
		goto out;
	}

	printf("-----------------------------------------\n");
	printf("[Measurement Result]\n");
	printf("-----------------------------------------\n");
	cpuUsage(END_CPU_CHECK);
	gettimeofday(&T2,NULL);
	print_time(T1, T2);

	if(num_threads == 1)
	{
		print_con_switches();	
	}
	
	/* Join threads */
	for(i = 0; i < num_threads; i++)
	{
		ret = pthread_join(thread_id[i], &res);
		if(ret < 0)
		{
			perror("pthread_join failed");
			//exit(EXIT_FAILURE);
			setState(ERROR, "pthread_join failed");
			return -1;
		}
		//free(res);
	}

	setState(END, NULL);

	if(Latency_state==1) fclose(Latency_fp); // close latency file
	if(print_IOPS==1) fclose(pIOPS_fp); // close latency file

out:

    shutdown_fs();

	printf("Err string : %s\n", g_err_string);

	return 0;
}
