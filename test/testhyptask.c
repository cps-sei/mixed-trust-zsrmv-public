#include <stdio.h>
#define __USE_GNU
#include <sched.h>
#undef __USE_GNU
#include <time.h>
#include <sys/types.h>
#include <signal.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h> 
#include <errno.h>
#include <sys/sem.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>

#include <zsrmvapi.h>

int main(int argc, char *argv[])
{
  int rid;
  int i;
  unsigned long long wcet;
  long l;
  int schedfd;
  int enffd;

  if ((schedfd = zsv_open_scheduler())<0){
    printf("Error opening the scheduler");
    return -1;
  }

  if (zsv_test_reserve(schedfd, 0)<0){
    printf("error \n");
  }


  sleep(8);

  if (zsv_test_reserve(schedfd, 1)<0){
    printf("error \n");
  }

}
