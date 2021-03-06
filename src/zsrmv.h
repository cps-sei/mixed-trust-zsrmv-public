/*
Mixed-Trust Kernel Module Scheduler
Copyright 2020 Carnegie Mellon University and Hyoseung Kim.
NO WARRANTY. THIS CARNEGIE MELLON UNIVERSITY AND SOFTWARE ENGINEERING INSTITUTE MATERIAL IS FURNISHED ON AN "AS-IS" BASIS. CARNEGIE MELLON UNIVERSITY MAKES NO WARRANTIES OF ANY KIND, EITHER EXPRESSED OR IMPLIED, AS TO ANY MATTER INCLUDING, BUT NOT LIMITED TO, WARRANTY OF FITNESS FOR PURPOSE OR MERCHANTABILITY, EXCLUSIVITY, OR RESULTS OBTAINED FROM USE OF THE MATERIAL. CARNEGIE MELLON UNIVERSITY DOES NOT MAKE ANY WARRANTY OF ANY KIND WITH RESPECT TO FREEDOM FROM PATENT, TRADEMARK, OR COPYRIGHT INFRINGEMENT.
Released under a BSD (SEI)-style license, please see license.txt or contact permission@sei.cmu.edu for full terms.
[DISTRIBUTION STATEMENT A] This material has been approved for public release and unlimited distribution.  Please see Copyright notice for non-US Government use and distribution.
Carnegie Mellon® is registered in the U.S. Patent and Trademark Office by Carnegie Mellon University.
DM20-0619
*/



#ifndef __ZSRMV_H__
#define __ZSRMV_H__ 1

#ifdef __KERNEL__
#include <linux/time.h>
#include <linux/hrtimer.h>
#else
#include <time.h>
#endif


struct zs_timer{
  //timer_t tid;
  int rid;
  int timer_type;
  struct timespec expiration;
  unsigned long long absolute_expiration_ns;
#ifdef __KERNEL__  
  struct hrtimer kernel_timer;
#endif
  struct zs_timer *next;

#ifdef STAC_FRAMAC_STUBS
  //-- ghost variable to indicate whether the timer is armed and the
  //-- time (relative to point of arming) when it will go off
  int stac_armed;
  unsigned long long stac_expiration_ns;
#ifdef __KERNEL__
  enum hrtimer_restart (*stac_handler)(struct hrtimer *timer);
#endif
#endif
};


struct reserve {
#ifdef __KERNEL__
  struct pid_namespace *task_namespace;
#endif
  pid_t  pid;
  int rid;
  unsigned long long first_job_activation_ns;
  unsigned long long job_activation_count;
  unsigned long long current_job_activation_ticks;
  unsigned long long current_job_deadline_ticks;
  int job_completed;
  unsigned long long current_job_hypertasks_preemption_ticks;
  unsigned long long start_ns;
  unsigned long long stop_ns;
  unsigned long long current_exectime_ns;
  unsigned long long exectime_ns;
  unsigned long long nominal_exectime_ns;
  unsigned long long exectime_in_rm_ns;

  unsigned long long start_ticks;
  unsigned long long stop_ticks;
  unsigned long long current_exectime_ticks;
  unsigned long long exectime_ticks;
  unsigned long long nominal_exectime_ticks;
  unsigned long long worst_exectime_ticks;
  unsigned long long avg_exectime_ticks;
  unsigned long avg_exectime_ticks_measurements;

  unsigned int num_period_wakeups;

  unsigned long num_enforcements;
  unsigned long num_wfnp;
  unsigned long num_wait_release;
  unsigned int non_periodic_wait;
  unsigned int end_of_period_marked;

#ifdef STAC_FRAMAC_STUBS
  //-- ghost variables we use to model the real execution time of a
  //-- job and start time of last job fragment
  unsigned long long real_exectime_ns;
  unsigned long long real_start_ns;
#endif

  int enforcement_type;
  struct timespec period;
  struct timespec zsinstant;
  struct timespec hyp_enforcer_instant;
  unsigned long long hyp_enforcer_instant_ns;
  unsigned long long hyp_enforcer_instant_ticks;
  unsigned long long zsinstant_ns;
  unsigned long long period_ns;
  unsigned long long period_ticks;
  struct timespec execution_time;
  struct timespec nominal_execution_time;
  int has_zsenforcement;
  int hypertask_active;
  int has_hyptask;
  uint32_t hyptask_handle; // u32
  int priority;
  int criticality;
  int in_critical_mode;
  int enforced;
  int bound_to_cpu;
  struct zs_timer period_timer;
  struct zs_timer enforcement_timer;
  struct zs_timer zero_slack_timer;
  struct zs_timer start_timer;
  struct reserve *next;
  struct reserve *rm_next;
  struct reserve *crit_next;
  struct reserve *crit_block_next;
  int request_stop;
  int enforcement_signal_captured;
  int enforcement_signal_receiver_pid;
  int enforcement_signo;
  int attached;

  // Some debugging variables
  int start_period;
}; 


// signatures for unit testing
int isHigherPrioHigherCrit(struct reserve *thisone, struct reserve *other);
int isLowerPrioHigherCrit(struct reserve *thisone, struct reserve *other);
int isHigherPrioSameCrit(struct reserve *thisone, struct reserve *other);
int isHigherPrioLowerCrit(struct reserve *thisone, struct reserve *other);
int getNextInSet(struct reserve *rsvtable, int *cidx, int tablesize, struct reserve *newrsv, int (*inSet)(struct reserve *t, struct reserve *o));
unsigned long long getExecTimeHigherPrioHigherCrit(struct reserve *r);
unsigned long long getExecTimeHigherPrioSameCrit(struct reserve *r);
unsigned long long getExecTimeLowerPrioHigherCrit(struct reserve *r);
unsigned long long getResponseTimeCritNs(struct reserve *rsvtable, int tablesize, struct reserve *newrsv);

// signatures for admission test
int admit(struct reserve *rsvtable, int tablesize, struct reserve *newrsv, unsigned long long *calcZ);


#endif // __ZSRMV_H__
