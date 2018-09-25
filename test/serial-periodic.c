/*
Copyright (c) 2014 Carnegie Mellon University.

All Rights Reserved.

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the 
following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following 
acknowledgments and disclaimers.
2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following 
acknowledgments and disclaimers in the documentation and/or other materials provided with the distribution.
3. Products derived from this software may not include “Carnegie Mellon University,” "SEI” and/or “Software 
Engineering Institute" in the name of such derived product, nor shall “Carnegie Mellon University,” "SEI” and/or 
“Software Engineering Institute" be used to endorse or promote products derived from this software without prior 
written permission. For written permission, please contact permission@sei.cmu.edu.

ACKNOWLEDMENTS AND DISCLAIMERS:
Copyright 2014 Carnegie Mellon University
This material is based upon work funded and supported by the Department of Defense under Contract No. FA8721-
05-C-0003 with Carnegie Mellon University for the operation of the Software Engineering Institute, a federally 
funded research and development center.

Any opinions, findings and conclusions or recommendations expressed in this material are those of the author(s) and 
do not necessarily reflect the views of the United States Department of Defense.

NO WARRANTY. 
THIS CARNEGIE MELLON UNIVERSITY AND SOFTWARE ENGINEERING INSTITUTE 
MATERIAL IS FURNISHED ON AN “AS-IS” BASIS. CARNEGIE MELLON UNIVERSITY MAKES NO 
WARRANTIES OF ANY KIND, EITHER EXPRESSED OR IMPLIED, AS TO ANY MATTER INCLUDING, 
BUT NOT LIMITED TO, WARRANTY OF FITNESS FOR PURPOSE OR MERCHANTABILITY, 
EXCLUSIVITY, OR RESULTS OBTAINED FROM USE OF THE MATERIAL. CARNEGIE MELLON 
UNIVERSITY DOES NOT MAKE ANY WARRANTY OF ANY KIND WITH RESPECT TO FREEDOM FROM 
PATENT, TRADEMARK, OR COPYRIGHT INFRINGEMENT.

This material has been approved for public release and unlimited distribution.
Carnegie Mellon® is registered in the U.S. Patent and Trademark Office by Carnegie Mellon University.

DM-0000891
*/

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
  int schedfd;
  int rid;
  
  char buffer[20];
  int len=20;
  int i;

  if ((schedfd = zsv_open_scheduler())<0){
    printf("Error opening the scheduler");
    return -1;
  }

  zsv_mtserial_init(schedfd, 115200);

  if ((rid = zsv_create_reserve(schedfd,
				1, // period_secs
				0, // period_nsecs
				1, // zsinstant_sec -- same as period = disabled
				0, // zsinstant_nsec -- same as period = disabled
				0, // hypertask enforcement sec
				800000000, // hypertask enforcement nsec
				0, // exectime _secs
				200000000, // exectime_nsecs
				0, // nominal_exectime_sec -- same as overloaded
				200000000, // nominal_exectime_nsec -- same as overloaded
				10, // priority
				1  // criticality
				) 
       )<0){
    printf("error calling create reserve\n");
    return -1;
  }  


  if (zsv_attach_reserve(schedfd, getpid(), rid)<0){
    printf("error calling attach reserve\n");
    return -1;
  }
 
  
  for (i=0;i<10;i++){
    if (i==3){
      usleep(1500000);
    }
    sprintf(buffer,"%d\n\r",i);
    if (zsv_mtserial_send_finish(schedfd, rid, buffer, strlen(buffer))<0){
      printf("error sending\n");
    }
  }

  zsv_delete_reserve(schedfd, rid);
  
  zsv_close_scheduler(schedfd);

  printf("Done\n");
}
