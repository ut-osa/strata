#include <stdio.h>
#include <assert.h>
#include <sys/time.h>
// Small programing to test functionality of UNIX timval related operations
void print_time(const char* name, struct timeval *val) {
   printf("%s: %ld.%06ld\n", name, val->tv_sec, val->tv_usec);
}

int main() {
   struct timeval current_time; 
   struct timeval delta;
   struct timeval result;
   timerclear(&delta);
   delta.tv_usec = 999999;
   gettimeofday(&current_time, NULL);
   print_time("current:", &current_time);
   struct timeval prev_time = current_time;
   timeradd(&current_time, &delta, &current_time);
   print_time("prev:", &prev_time);
   print_time("current_time:", &current_time);
   assert(timercmp(&prev_time, &current_time, <));
}
