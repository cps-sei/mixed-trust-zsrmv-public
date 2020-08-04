/*
Mixed-Trust Kernel Module Scheduler
Copyright 2020 Carnegie Mellon University and Hyoseung Kim.
NO WARRANTY. THIS CARNEGIE MELLON UNIVERSITY AND SOFTWARE ENGINEERING INSTITUTE MATERIAL IS FURNISHED ON AN "AS-IS" BASIS. CARNEGIE MELLON UNIVERSITY MAKES NO WARRANTIES OF ANY KIND, EITHER EXPRESSED OR IMPLIED, AS TO ANY MATTER INCLUDING, BUT NOT LIMITED TO, WARRANTY OF FITNESS FOR PURPOSE OR MERCHANTABILITY, EXCLUSIVITY, OR RESULTS OBTAINED FROM USE OF THE MATERIAL. CARNEGIE MELLON UNIVERSITY DOES NOT MAKE ANY WARRANTY OF ANY KIND WITH RESPECT TO FREEDOM FROM PATENT, TRADEMARK, OR COPYRIGHT INFRINGEMENT.
Released under a BSD (SEI)-style license, please see license.txt or contact permission@sei.cmu.edu for full terms.
[DISTRIBUTION STATEMENT A] This material has been approved for public release and unlimited distribution.  Please see Copyright notice for non-US Government use and distribution.
Carnegie Mellon® is registered in the U.S. Patent and Trademark Office by Carnegie Mellon University.
DM20-0619
*/


#ifndef _ZSRMVAPI_H_

#ifndef __KERNEL__
#include <unistd.h>
#include <sys/syscall.h>
#include <stdio.h>
#ifdef SYS_gettid
#define gettid() syscall(SYS_gettid)
#else
#error "SYS_gettid unavailable on this system"
#endif
#endif

#define _ZSRMVAPI_H_
#define WAIT_PERIOD 1
#define CREATE_RSV  2
#define ATTACH_RSV  3
#define DELETE_RSV  4
#define GET_WCET_NS 5
#define GET_ACET_NS 6
#define NOWAIT_PERIOD 7
#define CAPTURE_ENFORCEMENT_SIGNAL 8
#define GET_TRACE_SIZE 9
#define END_PERIOD 10
#define WAIT_RELEASE 11
#define TEST_RESERVE 12
#define SEND_SERIAL 13
#define SEND_SERIAL_FINISH 14
#define INIT_SERIAL 15
#define RECV_SERIAL 16
#define SIM_CRASH 17

#define STRING_ZSV_CALL(c) ( c == WAIT_PERIOD ? "wait_period" : \
			     c == CREATE_RSV  ? "create_rsv"  : \
			     c == ATTACH_RSV  ? "attach_rsv"  : \
			     c == DELETE_RSV  ? "delete_rsv"  : \
			     c == GET_WCET_NS ? "get_wcet_ns" : \
			     c == NOWAIT_PERIOD ? "nowait_period" : \
			     c == CAPTURE_ENFORCEMENT_SIGNAL ? "capture_enforcement_signal" : \
			     c == GET_TRACE_SIZE ? "get_trace_size" :\
			     c == END_PERIOD ? "end_period" : \
			     c == WAIT_RELEASE ? "wait_release" : \
			     "unknown")
#define ENF_NONE 0
#define ENF_BUDGET 1

#define ZS_BUDGET_ENFORCEMENT_SIGNAL (SIGRTMIN+1)

#define TRACE_EVENT_WFNP 1
#define TRACE_EVENT_START_PERIOD 2
#define TRACE_EVENT_PREEMPTED 3
#define TRACE_EVENT_RESUMED 4
#define TRACE_EVENT_BUDGET_ENFORCEMENT 5
#define TRACE_EVENT_DONT_WFNP 6
#define TRACE_EVENT_IDLE 7
#define TRACE_EVENT_END_PERIOD 8
#define TRACE_EVENT_WAIT_RELEASE 9
#define TRACE_EVENT_WAIT_RELEASE_BLOCKED 10
#define TRACE_EVENT_WAIT_RELEASE_NOT_BLOCKED 11
#define TRACE_EVENT_START_PERIOD_PERIODIC_WAIT 12
#define TRACE_EVENT_START_PERIOD_NON_PERIODIC_WAIT_WAKEUP 13
#define TRACE_EVENT_START_PERIOD_NON_PERIODIC_WAIT_NO_WAKEUP 14

#define STRING_TRACE_EVENT(e) ( e == TRACE_EVENT_WFNP ? "wait_period" : \
				e == TRACE_EVENT_START_PERIOD ? "start_period": \
				e == TRACE_EVENT_PREEMPTED ? "preempted" : \
				e == TRACE_EVENT_RESUMED ? "resumed" : \
				e == TRACE_EVENT_BUDGET_ENFORCEMENT ? "budget_enforced" :\
				e == TRACE_EVENT_DONT_WFNP ? "dont_wait_period": \
				e == TRACE_EVENT_IDLE      ? "idle" :\
				e == TRACE_EVENT_END_PERIOD ? "end_period" :\
				e == TRACE_EVENT_WAIT_RELEASE ? "wait_release" :\
				e == TRACE_EVENT_WAIT_RELEASE_BLOCKED ? "wait_release_blocked" : \
				e == TRACE_EVENT_WAIT_RELEASE_NOT_BLOCKED ? "wait_release_not_blocked" :\
				e == TRACE_EVENT_START_PERIOD_PERIODIC_WAIT ? "start_period_periodic_wait" :\
				e == TRACE_EVENT_START_PERIOD_NON_PERIODIC_WAIT_WAKEUP ? "start_period_non_periodic_wait_wakeup" :\
				e == TRACE_EVENT_START_PERIOD_NON_PERIODIC_WAIT_NO_WAKEUP ? "start_period_non_periodic_wait_no_wakeup" :\
				"unknown" \
				)

//#define BUNDLE_RID_STOP(rid,stop) ((rid+1) *(stop ? -1 : 1))
//#define EXTRACT_RID(b) ( b<0 ? (-1*b)-1 : b-1)
//#define EXTRACT_STOP(b) (b<0)

#define BUNDLE_RID_STOP_PERIODIC(rid,stop,periodic) ( (rid << 2) | (stop << 1) | periodic )
#define EXTRACT_RID(b) (b>>2)
#define EXTRACT_STOP(b) ( (b >> 1) & 0b1)
#define EXTRACT_PERIODIC(b) (b & 0b1)

struct trace_rec_t {
  unsigned long long timestamp_ns;
  int event_type;
  int rid;
};

struct threaded_signal_handler_table_t {
  int rid;
  void (*zsv_user_enforcement_handler)(void *p, int);
  void *zsv_user_enforcement_handler_param;
  struct threaded_signal_handler_table_t *next;
  struct threaded_signal_handler_table_t *prev;
  int zsv_enforcement_schedfd;
} ;

struct api_call {
  int rid;
  int pid;
  int cmd;
  long period_sec;
  long period_nsec;
  long zsinstant_sec;
  long zsinstant_nsec;
  long exec_sec;
  long exec_nsec;
  long nominal_exec_sec;
  long nominal_exec_nsec;
  long hyp_enforcer_sec;
  long hyp_enforcer_nsec;
  int priority;
  int criticality;
  unsigned long long *pwcet;
  void *buffer;
  int buf_len;
};

struct reserve_spec_t {
  long period_sec;
  long period_nsec;
  long zsinstant_sec;
  long zsinstant_nsec;
  long exec_sec;
  long exec_nsec;
  long nominal_exec_sec;
  long nominal_exec_nsec;
  int criticality;
};

int zsv_is_admissible(struct reserve_spec_t *reserves_specs_table, int tablesize);
int zsv_get_wcet_ns(int schedfd, int rid, unsigned long long *pwcet);
int zsv_get_acet_ns(int schedfd, int rid, unsigned long long *pacet);
void busy_timestamped(long millis, unsigned long long tsbuffer[], 
                      long bufsize, long *bufidx);
int zsv_create_reserve(int schedfd, long period_sec, long period_nsec,
		       long zsinstant_sec, long zsinstant_nsec,
		       long hyp_enforcer_sec, long hyp_enforcer_nsec,
		       long exec_sec, long exec_nsec,
		       long nominal_exec_sec, long nominal_exec_nsec,
		       int priority,
		       int criticality);
int zsv_attach_reserve(int schedfd, int pid, int rid);
int zsv_wait_period(int schedfd, int rid);
int zsv_nowait_period(int schedfd, int rid);
int zsv_delete_reserve(int schedfd, int rid);
int zsv_capture_budget_enforcement_signal(int schedfd, int rid, int pid, int signo);
int zsv_open_scheduler(void);
int zsv_close_scheduler(int fd);
unsigned long long get_now_ns(void);
int zsv_fork_enforcement_handler(int schedfd, int rid, void (*handler)(void *,int), void *param);
int zsv_stop_enforcement_handler(int rid);
struct threaded_signal_handler_table_t *get_threaded_signal_handler_record(int rid);
int add_threaded_signal_handler_record(struct threaded_signal_handler_table_t *rec);
int del_thraded_signal_handler_record(struct threaded_signal_handler_table_t *rec);
int zsv_wait_release(int schedfd, int rid);
int zsv_end_period(int schedfd, int rid);
int zsv_test_reserve(int schedfd, int option);
int zsv_mtserial_send(int schedfd, int rid, void *buffer, int length);
int zsv_mtserial_send_finish(int schedfd, int rid, void *buffer, int length);
int zsv_mtserial_recv(int schedfd, int rid, void *buffer, int length);
int zsv_mtserial_init(int schedfd, int bauds);
int zsv_simulate_crash(int schedfid);
#ifndef __KERNEL__
int zsv_write_trace(int schedfd, FILE *fid);
#endif
#endif
