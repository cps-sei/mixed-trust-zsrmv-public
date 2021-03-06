/*
Mixed-Trust Kernel Module Scheduler
Copyright 2020 Carnegie Mellon University and Hyoseung Kim.
NO WARRANTY. THIS CARNEGIE MELLON UNIVERSITY AND SOFTWARE ENGINEERING INSTITUTE MATERIAL IS FURNISHED ON AN "AS-IS" BASIS. CARNEGIE MELLON UNIVERSITY MAKES NO WARRANTIES OF ANY KIND, EITHER EXPRESSED OR IMPLIED, AS TO ANY MATTER INCLUDING, BUT NOT LIMITED TO, WARRANTY OF FITNESS FOR PURPOSE OR MERCHANTABILITY, EXCLUSIVITY, OR RESULTS OBTAINED FROM USE OF THE MATERIAL. CARNEGIE MELLON UNIVERSITY DOES NOT MAKE ANY WARRANTY OF ANY KIND WITH RESPECT TO FREEDOM FROM PATENT, TRADEMARK, OR COPYRIGHT INFRINGEMENT.
Released under a BSD (SEI)-style license, please see license.txt or contact permission@sei.cmu.edu for full terms.
[DISTRIBUTION STATEMENT A] This material has been approved for public release and unlimited distribution.  Please see Copyright notice for non-US Government use and distribution.
Carnegie Mellon® is registered in the U.S. Patent and Trademark Office by Carnegie Mellon University.
DM20-0619
*/


#include <linux/cdev.h>
#include <linux/version.h>
#include <linux/module.h>
#include <linux/moduleparam.h>

#include <linux/proc_fs.h>

#include <linux/kthread.h>
#include <linux/syscalls.h>
#include <linux/signal.h>

#include <linux/gpio.h>

#include <linux/clocksource.h>
#include <linux/timekeeping.h>
#include <linux/delay.h>

#include <asm/div64.h>

#include "hypmtscheduler.h"

//externals
extern  void __hvc(u32 uhcall_function, void *uhcall_buffer, u32 uhcall_buffer_len);
extern bool hypmtscheduler_createhyptask(u32 first_period, u32 regular_period,
			u32 priority, u32 hyptask_id, u32 *hyptask_handle);
extern bool hypmtscheduler_disablehyptask(u32 hyptask_handle);
extern bool hypmtscheduler_guestjobstart(u32 hyptask_handle);
extern bool hypmtscheduler_deletehyptask(u32 hyptask_handle);
extern u64 sysreg_read_cntpct(void);
extern bool hypmtscheduler_getrawtick64(u64 *tickcount);
extern u64 hypmtscheduler_readtsc64(void);
extern bool hypmtscheduler_inittsc(void);
extern bool hypmtscheduler_logtsc(u32 event);
extern bool hypmtscheduler_dumpdebuglog(u8 *dst_log_buffer, u32 *num_entries);


//////
// externals
//////
extern  void __hvc(u32 uhcall_function, void *uhcall_buffer, u32 uhcall_buffer_len);
extern void mavlinkserhb_initialize(u32 baudrate);
extern bool mavlinkserhb_send(u8 *buffer, u32 buf_len);
extern bool mavlinkserhb_checkrecv(void);
extern bool mavlinkserhb_recv(u8 *buffer, u32 max_len, u32 *len_read, bool *uartreadbufexhausted);
extern bool mavlinkserhb_activatehbhyptask(u32 first_period, u32 recurring_period,
		u32 priority);
extern bool mavlinkserhb_deactivatehbhyptask(void);


hypmtscheduler_logentry_t debug_log[DEBUG_LOG_SIZE];
u32 debug_log_buffer_index = 0;

#include "zsrmv.h"
#include "zsrmvapi.h"

//#define ZSV_SIMULATE_CRASH 1

//#define __ZSV_SECURE_TASK_BOOTSTRAP__ 1

#define __START_SERIAL_RECEIVER_TASK__ 1

#define __SERIAL_HARDWARE_CONTROL_FLOW__ 1

/*********************************************************************/
//-- variables to model time
/*********************************************************************/


#define GPIO_RTS 17
#define GPIO_CTS 6

static int cts_gpio_pin=GPIO_CTS;
module_param(cts_gpio_pin, int, 0660);

void serial_stop_transmission(void){
  gpio_set_value(GPIO_RTS, 1);//0);
}

void serial_resume_transmission(void){
  gpio_set_value(GPIO_RTS, 0);//1);
}

int serial_is_reception_stopped(void){
  return gpio_get_value(cts_gpio_pin);
}

// forward declarations
void create_hypertask(int rid);


//-- ghost variable: the current time
unsigned long long stac_now = 0;


/**
 * Variables to measure overhead
 */

unsigned long long hypercall_start_timestamp_ticks=0L;
unsigned long long hypercall_end_timestamp_ticks=0L;
unsigned long long cumm_hypercall_ticks=0L;
unsigned long long num_hypercalls = 0L;
unsigned long long wc_hypercall_ticks = 0L;

unsigned long long context_switch_start_timestamp_ticks=0L;
unsigned long long context_switch_end_timestamp_ticks=0L;
unsigned long long cumm_context_switch_ticks=0L;
unsigned long long num_context_switches = 0L;
unsigned long long wc_context_switch_ticks=0L;

unsigned long long enforcement_start_timestamp_ticks=0L;
unsigned long long enforcement_end_timestamp_ticks=0L;
unsigned long long cumm_enforcement_ticks = 0L;
unsigned long long num_enforcements = 0L;
unsigned long long wc_enforcement_ticks=0L;

unsigned long long zs_enforcement_start_timestamp_ticks=0L;
unsigned long long zs_enforcement_end_timestamp_ticks=0L;
unsigned long long cumm_zs_enforcement_ticks =0L;
unsigned long long num_zs_enforcements=0L;

unsigned long long arrival_start_timestamp_ticks=0L;
unsigned long long arrival_end_timestamp_ticks=0L;
unsigned long long cumm_arrival_ticks = 0L;
unsigned long long num_arrivals = 0L;
unsigned long long wc_arrival_ticks = 0L;

unsigned long long cumm_blocked_arrival_ticks=0L;
unsigned long long num_blocked_arrivals=0L;

unsigned long long departure_start_timestamp_ticks=0L;
unsigned long long departure_end_timestamp_ticks=0L;
unsigned long long cumm_departure_ticks=0L;
unsigned long long num_departures = 0L;
unsigned long long wc_departure_ticks=0L;

u64 start_tick;
u64 end_tick;


inline unsigned long long DIV(unsigned long long a, unsigned long long b){
  //uint64_t _a = a;
  unsigned long long _a =a;
  unsigned long _b = (unsigned long) b;
  do_div(_a,_b);
  //_a = _a / b;
  return _a;
}


/**
 *  Enable use of sys_tsc
 */
#define __ZS_USE_SYSTSC__ 1


/**
 * USE HYPERVISOR TSC
 */

//#define __ZS_USE_HYPTSC__ 1

/**
 * Enable or disable the timestamp counter
 */
#define __ZS_USE_TSC__ 1

/**
 * These functions need to be compiled only on ARM
 * Need to add conditional compilation macros
 */
static inline void ccnt_init (void)
{
    asm volatile ("mcr p15, 0, %0, c15, c12, 0" : : "r" (1));
}

static inline unsigned ccnt_read (void)
{
  unsigned cc;
  asm volatile ("mrc p15, 0, %0, c15, c12, 1" : "=r" (cc));
  return cc;
}

#define PMCNTNSET_C_BIT		0x80000000
#define PMCR_C_BIT		0x00000004
#define PMCR_E_BIT		0x00000001
#define PMCR_LC_BIT             (1UL<<6)

void init_cputsc(void){
	unsigned long tmp;

	tmp = PMCNTNSET_C_BIT;
	asm volatile ("mcr p15, 0, %0, c9, c12, 1" : : "r" (tmp));
	asm volatile ("mrc p15, 0, %0, c9, c12, 0" : "=r" (tmp));
	tmp |= PMCR_C_BIT | PMCR_E_BIT | PMCR_LC_BIT ;
	asm volatile ("mcr p15, 0, %0, c9, c12, 0" : : "r" (tmp));
}

u32 rdcntfrq(void){
	u32 frq;

	asm volatile
	  ("mrc p15, 0, r0, c14, c0, 0 \r\n"
	   "mov %0, r0 \r\n"
	   : "=r" (frq)  // output
	   : // inputs
	   : "r0" // clobber
	   );

	return frq;
}

u64 rdtsc64(void){
	u32 tsc_lo, tsc_hi;
	u64 l_tickcount;

	asm volatile
	  (	//" dsb\r\n" // data synchronization barrier *** testing to see if we need this
		//" isb\r\n" // instruction synchronization barrier
		// Syntax: MRRC <coproc>=coprocessor, <#opcode3>=coproc opcode, Rt, Rt2, CRm=coproc reg
		" mrrc p15, 0, r0, r1, c9 \r\n" // read 64 bits tsc
		" mov %0, r0 \r\n"
		" mov %1, r1 \r\n"
		: "=r" (tsc_lo), "=r" (tsc_hi) // outputs
		: // inputs
		: "r0", "r1" //clobber
	    );

	l_tickcount = tsc_hi;
	l_tickcount = l_tickcount << 32;
	l_tickcount |= tsc_lo;

	return l_tickcount;
}


/*************** END OF ARM ONLY ***********************/



/**
 * Trace structures
 */
#define TRACE_BUFFER_SIZE 100000
struct trace_rec_t trace_table[TRACE_BUFFER_SIZE];
int trace_index=0;



/*********************************************************************/
//-- variables and functions to model preemption
/*********************************************************************/

/*********************************************************************/
//-- ZSRM specific data structures
/*********************************************************************/

/**
 * DEBUG configurations
 */

//#define __ZS_DEBUG__ 1

// DEBUG VARIABLE

int calling_start_from=0;
#define NAME_START_FROM(n) (n == 1 ? "start_of_period" : \
			     n == 2 ? "attach_reserve " : \
			     n == 3 ? "wait_period" : \
			     n == 4 ? "scheduler_task" : \
			     "unknown")
int prev_calling_stop_from=0;
int calling_stop_from=0;
#define NAME_STOP_FROM(n) ( n == 1 ? "start_of_period" : \
			    n == 2 ? "delete_reserve"  : \
			    n == 3 ? "wait_period"     : \
			    n == 4 ? "scheduler_task"  : \
			    "unknown")


#define TIMER_ZS  1
#define TIMER_ENF 2
#define TIMER_ZS_ENF 3
#define TIMER_PERIOD 4
#define TIMER_START 5

// OTHER LOCKING SITUATIONS
#define SCHED_TASK 5
#define ZSV_CALL   6

#define STRING_TIMER_TYPE(t) ( t == TIMER_ENF ? "timer_enf" :\
			       t == TIMER_PERIOD ? "timer_period" :\
			       t == TIMER_ZS_ENF ? "timer_zs_enf" : \
			       t == TIMER_START  ?  "timer_start" : \
			       "unknown")

int prevlocker=0;
int zsrmcall=-1;

#define STRING_LOCKER(t) ( t == TIMER_ENF ? "timer_enf" :\
			   t == TIMER_PERIOD ? "timer_period" :	    \
			   t == TIMER_ZS_ENF ? "timer_zs_enf" :	    \
			   t == SCHED_TASK   ? "sched_task" : \
			   t == ZSV_CALL     ? "zsv_call"  :\
			   t == 0            ? "none" : \
			   "unknown")


#define MAX_RESERVES 100

#define MIN_PRIORITY 50

#define DAEMON_PRIORITY (MIN_PRIORITY + 40)
#define RECEIVER_PRIORITY (DAEMON_PRIORITY +1)

struct task_struct *sched_task;
struct task_struct *active_task;
struct task_struct *serial_recv_task;
struct task_struct *serial_sender_task;

u32 serial_debug_flags=0;
int serial_receiving_error_count=0;
int serial_debug_last_non_zero_receive_count=0;
unsigned long long  serial_debug_num_zero_receive_counts=0L;
int serial_debug_last_receive_count=0;
int serial_debug_largest_read_count=0;
unsigned long long serial_debug_max_sleep_ticks=0L;
unsigned long long serial_debug_before_sleep_timestamp_ticks=0L;
unsigned long long serial_debug_after_sleep_timestamp_ticks=0L;
unsigned long long serial_debug_sleep_elapsed_interval_ticks=0L;

#define SERIAL_FLAG_RCV_READ_BLOCKED 1
#define SERIAL_FLAG_SND_READ_BLOCKED 2
#define SERIAL_FLAG_HWR_RCV_BLOCKED 4
#define SERIAL_FLAG_HWR_SND_BLOCKED 8

inline void SERIAL_DEBUG_FLAG_ON(u32 FLAG) {
  serial_debug_flags |= FLAG;
}
inline void SERIAL_DEBUG_FLAG_OFF(u32 FLAG){
  serial_debug_flags &= ~(FLAG);
}

struct reserve reserve_table[MAX_RESERVES];

unsigned long long kernel_entry_timestamp_ticks=0;

// only tasks with higher or equal criticality than sys_criticality are allowed
// to run
int sys_criticality = 0;

struct reserve *critical_reservesq=NULL;
struct reserve *crit_blockq=NULL;
struct reserve *readyq=NULL;
struct reserve *rm_head=NULL;
int rm_queue_size=0;

int sync_start_semid;
int ready_semid;

/*********************************************************************/
// -----FUNCTION SIGNATURES
/*********************************************************************/

unsigned long long get_now_ns(void);
unsigned long long get_now_ticks(void);
int getreserve(void);
void budget_enforcement(int rid, int request_stop);
void start_of_period(int rid);
int timer_handler(struct zs_timer *timer);
int add_timerq(struct zs_timer *t);
int attach_reserve(int rid, int pid);
int start_enforcement_timer(struct reserve *rsvp);
void start_stac(int rid);
void start(int rid);
void stop_stac(int rid);
void stop(int rid);
int wait_for_next_period(int rid, int nowait, int disableHypertask);
int calculate_rm_priorities(void);
int set_rm_priorities(void);
struct task_struct *gettask(int pid, struct pid_namespace *ns);
float compute_total_utilization(void);
int push_to_reschedule(int i);
int pop_to_reschedule(void);
void init(void);
enum hrtimer_restart kernel_timer_handler(struct hrtimer *timer);
unsigned long long ticks2ns(unsigned long long ticks);
unsigned long long ticks2ns1(unsigned long long ticks);
unsigned long long ns2ticks(unsigned long long ns);
void init_reserve(int rid);
int get_acet_ns(int rid, unsigned long long *avet);
int get_wcet_ns(int rid, unsigned long long *wcet);
void reset_exectime_counters(int rid);
int delete_reserve(int rid);
int add_trace_record(int rid, unsigned long long ts, int event_type);
int in_readyq(int rid);
int push_to_activate(int i);
int pop_to_activate(void);
int end_of_period(int rid);
int wait_for_next_release(int rid);
/*********************************************************************/
// ---- END OF FUNCTION SIGNATURES
/*********************************************************************/

//@logic int maxReserves = (int)100;

/*@predicate elem(struct reserve *p) = \exists int i; (0 <= i < maxReserves && p == &(reserve_table[i]));*/
/*@predicate elemNull(struct reserve *p) = (p == \null) || elem(p);*/
/*@predicate disc(struct reserve *p) = \forall int i; 0 <= i < maxReserves ==> reserve_table[i].next != p;*/

/*@predicate elemt(struct zs_timer *p) = \exists int i; (0 <= i < maxReserves &&
  (p == &(reserve_table[i].period_timer) || p == &(reserve_table[i].enforcement_timer)));*/
/*@predicate elemtNull(struct zs_timer *p) = (p == \null) || elemt(p);*/
/*@predicate disct(struct zs_timer *p) = \forall int i; 0 <= i < maxReserves ==>
  (reserve_table[i].period_timer.next != p &&
  reserve_table[i].enforcement_timer.next != p);*/

/*@predicate fp11 = elemNull(readyq);*/
/*@predicate fp12 = elemNull(rm_head);*/
/*@predicate fp13 = \forall int i; 0 <= i < maxReserves ==>
  reserve_table[i].rid == i;*/
/*@predicate fp1 = fp11 && fp12 && fp13;*/

/*@predicate fp21 = \forall struct reserve *p; elem(p) ==> elemNull(p->next);*/
/*@predicate fp22 = \forall struct reserve *p; elem(p) ==> elemNull(p->rm_next);*/
/*@predicate fp23 = \forall struct zs_timer *p; elemt(p) ==> elemtNull(p->next);*/
/*@predicate fp2 = fp21 && fp22 && fp23;*/

/*@predicate fp31 = \forall struct reserve *p, *q; elem(p) ==> elem(q) ==>
  (p->next == q) ==> (p->priority >= q->priority);*/
/*@predicate fp32 = \forall struct reserve *p, *q; elem(p) ==> elem(q) ==>
  (p->rm_next == q) ==> (p->period_ns <= q->period_ns);*/
/*@predicate fp3 = fp31 && fp32;*/

/*@predicate fp = fp1 && fp2 && fp3;*/

///*@global invariant fpi : fp11 && fp12 && fp13 && fp21 && fp22 && fp23 && fp31 && fp32;*/

//-- ZSRM invariants

/*@predicate zsrm_lem11 = \forall int i; 0 <= i < maxReserves ==>
  ((readyq != &(reserve_table[i]) ==> \separated(readyq,&(reserve_table[i]))) &&
  (\forall int j; 0 <= j < maxReserves ==> reserve_table[j].next != &(reserve_table[i]) ==> \separated(reserve_table[j].next,&(reserve_table[i]))));*/

/*@predicate zsrm_lem12 = \forall struct reserve *p, struct zs_timer *q; \separated(p,q);*/

/*@predicate zsrm_lem1 = zsrm_lem11 && zsrm_lem12;*/

//-- real_exec_time <= current_exec_time && start_ns <= real_start_ns
/*@predicate zsrm1 = \forall int i; 0 <= i < maxReserves ==>
  (reserve_table[i].real_exectime_ns <= reserve_table[i].current_exectime_ns &&
  reserve_table[i].start_ns <= reserve_table[i].real_start_ns);*/

//-- real_exec_time <= current_exec_time && start_ns <= real_start_ns
/*@predicate zsrm1_stop1 = \forall int i; 0 <= i < maxReserves ==>
  ((readyq != &(reserve_table[i]) ==> reserve_table[i].real_exectime_ns <= reserve_table[i].current_exectime_ns) &&
  (readyq == &(reserve_table[i]) ==> reserve_table[i].real_exectime_ns <= reserve_table[i].current_exectime_ns + stac_now - readyq->real_start_ns) &&
  reserve_table[i].start_ns <= reserve_table[i].real_start_ns);*///-- real_exec_time <= current_exec_time && start_ns <= real_start_ns

/*@predicate zsrm1_stop2 = \forall int i; 0 <= i < maxReserves ==>
  (reserve_table[i].real_exectime_ns <= reserve_table[i].current_exectime_ns &&
  (readyq != &(reserve_table[i]) ==> reserve_table[i].start_ns <= reserve_table[i].real_start_ns) &&
  (readyq == &(reserve_table[i]) ==> reserve_table[i].start_ns <= stac_now));*/

//-- real_exec_time <= current_exec_time && start_ns <= real_start_ns
/*@predicate zsrm1_start1 = \forall int i; 0 <= i < maxReserves ==>
  ((readyq != &(reserve_table[i]) ==> reserve_table[i].real_exectime_ns <= reserve_table[i].current_exectime_ns) &&
  (readyq == &(reserve_table[i]) ==> reserve_table[i].real_exectime_ns <= reserve_table[i].current_exectime_ns + stac_now - readyq->real_start_ns) &&
  reserve_table[i].start_ns <= reserve_table[i].real_start_ns);*///-- real_exec_time <= current_exec_time && start_ns <= real_start_ns

/*@predicate zsrm1_start2 = \forall int i; 0 <= i < maxReserves ==>
  (reserve_table[i].real_exectime_ns <= reserve_table[i].current_exectime_ns &&
  (readyq != &(reserve_table[i]) ==> reserve_table[i].start_ns <= reserve_table[i].real_start_ns) &&
  (readyq == &(reserve_table[i]) ==> reserve_table[i].start_ns <= stac_now));*/

//-- the active job must have a timer set to enforce its execution time
/*@predicate zsrm2 = elem(readyq) ==>
  (((readyq->enforcement_timer.expiration.tv_sec * 1000000000L +
  readyq->enforcement_timer.expiration.tv_nsec) <=
  (readyq->exectime_ns - readyq->current_exectime_ns)) &&
  ((readyq->enforcement_timer.expiration.tv_sec * 1000000000L +
  readyq->enforcement_timer.expiration.tv_nsec) <=
  (readyq->exectime_ns - readyq->real_exectime_ns)));*/

//-- current_exec_time <= exec_time
/*@predicate zsrm3 = \forall int i; 0 <= i < maxReserves ==>
  reserve_table[i].current_exectime_ns <= reserve_table[i].exectime_ns;*/

//-- the active job must have a timer set to enforce its execution time
/*@predicate zsrm4 = \forall int i; 0 <= i < maxReserves ==>
  (reserve_table[i].enforcement_timer.expiration.tv_sec * 1000000000L +
  reserve_table[i].enforcement_timer.expiration.tv_nsec) <=
  reserve_table[i].exectime_ns;*/

//-- the active job must have a timer set to enforce its execution time
/*@predicate zsrm5 = elem(readyq) ==>
  ((readyq->enforcement_timer.absolute_expiration_ns <=
  (readyq->exectime_ns - readyq->current_exectime_ns)) &&
  (readyq->enforcement_timer.absolute_expiration_ns <=
  (readyq->exectime_ns - readyq->real_exectime_ns)));*/

//-- the active job must have a timer set to enforce its execution time
/*@predicate zsrm6 = elem(readyq) ==>
  ((readyq->enforcement_timer.stac_expiration_ns <=
  (readyq->exectime_ns - readyq->current_exectime_ns)) &&
  (readyq->enforcement_timer.stac_armed == 1) &&
  (readyq->enforcement_timer.stac_handler == kernel_timer_handler));*/

//-- periods must equal expiration time of period timer, and timer
//-- rids must equal the reservation ids
/*@predicate zsrm7 = \forall int i; 0 <= i < maxReserves ==>
  (reserve_table[i].period_timer.expiration.tv_sec == reserve_table[i].period.tv_sec &&
  reserve_table[i].period_timer.expiration.tv_nsec == reserve_table[i].period.tv_nsec &&
  reserve_table[i].period_timer.rid == i &&
  reserve_table[i].enforcement_timer.rid == i);*/

//-- period timer must be armed properly
/*@predicate zsrm8 (int i) =
  (reserve_table[i].period_timer.stac_armed == 1 &&
  reserve_table[i].period_timer.stac_expiration_ns == reserve_table[i].period.tv_sec * 1000000000L + reserve_table[i].period.tv_nsec &&
  reserve_table[i].period_timer.stac_handler == kernel_timer_handler);*/

/*********************************************************************/
//-- functions to model time
/*********************************************************************/

//-- exit the program
/*@requires \true;
  @assigns \nothing;
  @ensures \false;
*/
void stac_exit(void)
{
#ifdef STAC_FRAMAC_STUBS
  /*@loop invariant \true;
    @loop assigns \nothing;
  */
  while(1) {}
#endif
}

/*********************************************************************/
//-- system call stubs
/*********************************************************************/

#ifdef STAC_FRAMAC_STUBS

/*********************************************************************/
/*@requires fp11 && fp12 && fp13;
  @requires fp21 && fp22 && fp23;
  @requires fp31 && fp32;
  @requires zsrm1 && zsrm2 && zsrm3 && zsrm4 && zsrm7;
  @assigns \nothing;
  @ensures fp11 && fp12 && fp13;
  @ensures fp21 && fp22 && fp23;
  @ensures fp31 && fp32;
  @ensures zsrm1 && zsrm2 && zsrm3 && zsrm4 && zsrm7;
*/
/*********************************************************************/
int sched_setscheduler(struct task_struct *, int,
                       const struct sched_param *);

/*********************************************************************/
/*@requires fp11 && fp12 && fp13;
  @requires fp21 && fp22 && fp23;
  @requires fp31 && fp32;
  @requires zsrm1 && zsrm2 && zsrm3 && zsrm4 && zsrm7;
  @assigns \nothing;
  @ensures fp11 && fp12 && fp13;
  @ensures fp21 && fp22 && fp23;
  @ensures fp31 && fp32;
  @ensures zsrm1 && zsrm2 && zsrm3 && zsrm4 && zsrm7;
*/
/*********************************************************************/
ktime_t ktime_set(const s64 secs, const unsigned long nsecs);

/*********************************************************************/
/*@requires fp11 && fp12 && fp13;
  @requires fp21 && fp22 && fp23;
  @requires fp31 && fp32;
  @requires zsrm1 && zsrm2 && zsrm3 && zsrm4 && zsrm7;
  @assigns \nothing;
  @ensures fp11 && fp12 && fp13;
  @ensures fp21 && fp22 && fp23;
  @ensures fp31 && fp32;
  @ensures zsrm1 && zsrm2 && zsrm3 && zsrm4 && zsrm7;
*/
/*********************************************************************/
int printk(const char *fmt, ...);

/*********************************************************************/
/*@requires fp11 && fp12 && fp13;
  @requires fp21 && fp22 && fp23;
  @requires fp31 && fp32;
  @requires zsrm1 && zsrm2 && zsrm3 && zsrm4 && zsrm7;
  @assigns \nothing;
  @ensures fp11 && fp12 && fp13;
  @ensures fp21 && fp22 && fp23;
  @ensures fp31 && fp32;
  @ensures zsrm1 && zsrm2 && zsrm3 && zsrm4 && zsrm7;
*/
/*********************************************************************/
void hrtimer_init(struct hrtimer *timer, clockid_t which_clock,
                  enum hrtimer_mode mode);

/*********************************************************************/
/*@requires fp11 && fp12 && fp13;
  @requires fp21 && fp22 && fp23;
  @requires fp31 && fp32;
  @requires zsrm1 && zsrm2 && zsrm3 && zsrm4 && zsrm7;
  @assigns \nothing;
  @ensures fp11 && fp12 && fp13;
  @ensures fp21 && fp22 && fp23;
  @ensures fp31 && fp32;
  @ensures zsrm1 && zsrm2 && zsrm3 && zsrm4 && zsrm7;
*/
/*********************************************************************/
int hrtimer_start(struct hrtimer *timer, ktime_t tim,
                  const enum hrtimer_mode mode);

/*********************************************************************/
/*@requires fp11 && fp12 && fp13;
  @requires fp21 && fp22 && fp23;
  @requires fp31 && fp32;
  @requires zsrm_lem1 && zsrm1 && zsrm3 && zsrm4 && zsrm7;
  @assigns \nothing;
  @ensures fp11 && fp12 && fp13;
  @ensures fp21 && fp22 && fp23;
  @ensures fp31 && fp32;
  @ensures zsrm_lem1 && zsrm1 && zsrm3 && zsrm4 && zsrm7;
  @behavior b1: @assumes \true; @ensures \true;
  @behavior b2: @assumes zsrm2; @ensures zsrm2;
*/
/*********************************************************************/
int hrtimer_cancel(struct hrtimer *timer);

/*********************************************************************/
/*@requires fp11 && fp12 && fp13;
  @requires fp21 && fp22 && fp23;
  @requires fp31 && fp32;
  @requires zsrm1 && zsrm2 && zsrm3 && zsrm4 && zsrm7;
  @assigns \nothing;
  @ensures fp11 && fp12 && fp13;
  @ensures fp21 && fp22 && fp23;
  @ensures fp31 && fp32;
  @ensures zsrm1 && zsrm2 && zsrm3 && zsrm4 && zsrm7;
*/
/*********************************************************************/
struct task_struct *gettask(int pid,struct pid_namespace *ns);

/*********************************************************************/
/*@requires fp11 && fp12 && fp13;
  @requires fp21 && fp22 && fp23;
  @requires fp31 && fp32;
  @requires zsrm1 && zsrm2 && zsrm3 && zsrm4 && zsrm7;
  @assigns \nothing;
  @ensures fp11 && fp12 && fp13;
  @ensures fp21 && fp22 && fp23;
  @ensures fp31 && fp32;
  @ensures zsrm1 && zsrm2 && zsrm3 && zsrm4 && zsrm7;
*/
/*********************************************************************/
int push_to_reschedule(int i);

/*********************************************************************/
/*@requires fp11 && fp12 && fp13;
  @requires fp21 && fp22 && fp23;
  @requires fp31 && fp32;
  @requires zsrm1 && zsrm2 && zsrm3 && zsrm4 && zsrm7;
  @assigns \nothing;
  @ensures fp11 && fp12 && fp13;
  @ensures fp21 && fp22 && fp23;
  @ensures fp31 && fp32;
  @ensures zsrm1 && zsrm2 && zsrm3 && zsrm4 && zsrm7;
*/
/*********************************************************************/
int wake_up_process(struct task_struct *tsk);

/*********************************************************************/
/*@requires fp11 && fp12 && fp13;
  @requires fp21 && fp22 && fp23;
  @requires fp31 && fp32;
  @requires zsrm1 && zsrm2 && zsrm3 && zsrm4 && zsrm7;
  @assigns \nothing;
  @ensures fp11 && fp12 && fp13;
  @ensures fp21 && fp22 && fp23;
  @ensures fp31 && fp32;
  @ensures zsrm1 && zsrm2 && zsrm3 && zsrm4 && zsrm7;
*/
/*********************************************************************/
void set_tsk_need_resched(struct task_struct *tsk);

/*********************************************************************/
/*@requires fp11 && fp12 && fp13;
  @requires fp21 && fp22 && fp23;
  @requires fp31 && fp32;
  @requires zsrm1 && zsrm2 && zsrm3 && zsrm4 && zsrm7;
  @assigns \nothing;
  @ensures fp11 && fp12 && fp13;
  @ensures fp21 && fp22 && fp23;
  @ensures fp31 && fp32;
  @ensures zsrm1 && zsrm2 && zsrm3 && zsrm4 && zsrm7;
*/
/*********************************************************************/
int set_cpus_allowed_ptr(struct task_struct *p,
                         const struct cpumask *new_mask);

/*********************************************************************/
/*@requires fp11 && fp12 && fp13;
  @requires fp21 && fp22 && fp23;
  @requires fp31 && fp32;
  @requires zsrm1 && zsrm2 && zsrm3 && zsrm4 && zsrm7;
  @assigns \nothing;
  @ensures fp11 && fp12 && fp13;
  @ensures fp21 && fp22 && fp23;
  @ensures fp31 && fp32;
  @ensures zsrm1 && zsrm2 && zsrm3 && zsrm4 && zsrm7;
*/
/*********************************************************************/
void __xchg_wrong_size(void);

#endif //STAC_FRAMAC_STUBS


/*********************************************************************/
//-- ZSRM functions
/*********************************************************************/

int add_trace_record(int rid, unsigned long long ts, int event_type)
{
#ifdef ZSV_SIMULATE_CRASH
  char buf[100];
#endif

/* #ifdef __ZS_USE_SYSTSC__ */
/*   if (trace_index >= TRACE_BUFFER_SIZE) */
/*     return -1; */

/*   trace_table[trace_index].timestamp_ns = sysreg_read_cntpct(); //ts; */
/*   trace_table[trace_index].event_type = event_type; */
/*   trace_table[trace_index].rid = rid; */
/*   trace_index++; */
/*   return 0;   */
/* #elif __ZS_USE_HYPTSC__ */

/*   hypmtscheduler_logtsc(event_type); */

/* #else */

  if (trace_index >= TRACE_BUFFER_SIZE)
    return -1;

  trace_table[trace_index].timestamp_ns = ts;
  trace_table[trace_index].event_type = event_type;
  trace_table[trace_index].rid = rid;
  trace_index++;

#ifdef ZSV_SIMULATE_CRASH
  // Do not print the hypervisor trace events -- they will be printed from the hypervisor
  if (event_type < 50){
    sprintf(buf,"%d 0x%llx 0x%x\n",rid,ts,event_type);
    mavlinkserhb_send(buf,strlen(buf));
  }
#endif

  return 0;
/* #endif */
}

/*********************************************************************/
/*@requires fp11 && fp12 && fp13;
  @requires fp21 && fp22 && fp23;
  @requires fp31 && fp32;
  @requires zsrm1 && zsrm2 && zsrm3 && zsrm4 && zsrm7;
  @requires elemt(timer);
  @requires timer->absolute_expiration_ns == timer->expiration.tv_sec * 1000000000L + timer->expiration.tv_nsec;
  @assigns *timer;
  @ensures fp11 && fp12 && fp13;
  @ensures fp21 && fp22 && fp23;
  @ensures fp31 && fp32;
  @ensures zsrm1 && zsrm2 && zsrm3 && zsrm4 && zsrm7;
  @behavior b1:
  @assumes timer == &(readyq->enforcement_timer) && zsrm5;
  @ensures zsrm5 && zsrm6;
  @behavior b2:
  @assumes \exists int i; 0 <= i < maxReserves && timer == &(reserve_table[i].period_timer);
  @ensures timer->stac_expiration_ns == timer->expiration.tv_sec * 1000000000L + timer->expiration.tv_nsec;
  @ensures timer->stac_armed == 1 && timer->stac_handler == kernel_timer_handler;
  @ensures timer->stac_expiration_ns == reserve_table[timer->rid].period.tv_sec * 1000000000L + reserve_table[timer->rid].period.tv_nsec;
*/
/*********************************************************************/
int arm_relative_timer(struct zs_timer *timer)
{
  ktime_t ktime;

  ktime = ns_to_ktime(timer->absolute_expiration_ns);

  /* ktime = ktime_set(timer->absolute_expiration_ns / 1000000000, */
  /*                   timer->absolute_expiration_ns % 1000000000); */

#if 0
#ifdef __ZS_DEBUG__
  printk("ZSRMV: arm_timer (%llu secs, %llu ns ) type(%s)\n", (timer->absolute_expiration_ns  / 1000000000),
	 (timer->absolute_expiration_ns % 1000000000),
	  timer->timer_type == TIMER_ENF ? "TIMER_ENF" : timer->timer_type == TIMER_PERIOD ? "TIMER_PERIOD" : "TIMER_ZS_ENF");
#endif
#endif

  /* hrtimer_init(&(timer->kernel_timer), CLOCK_MONOTONIC, HRTIMER_MODE_REL); */
  timer->kernel_timer.function= kernel_timer_handler;
  hrtimer_start(&(timer->kernel_timer), ktime, HRTIMER_MODE_REL);

  //-- update ghost variables
#ifdef STAC_FRAMAC_STUBS
  timer->stac_armed = 1;
  timer->stac_expiration_ns = timer->absolute_expiration_ns;
  timer->stac_handler = timer->kernel_timer.function;
#endif

  return 0;
}

/*********************************************************************/
/*@requires fp11 && fp12 && fp13;
  @requires fp21 && fp22 && fp23;
  @requires fp31 && fp32;
  @requires zsrm_lem1 && zsrm1 && zsrm2 && zsrm3 && zsrm4 && zsrm7;
  @requires elemt(t);
  @assigns reserve_table[0..(maxReserves-1)],stac_now;
  @ensures stac_now >= \old(stac_now);
  @ensures fp11 && fp12 && fp13;
  @ensures fp21 && fp22 && fp23;
  @ensures fp31 && fp32;
  @ensures zsrm_lem1 && zsrm1 && zsrm2 && zsrm3 && zsrm4 && zsrm7;
  @behavior b1:
  @assumes t == &(readyq->enforcement_timer); @ensures zsrm5 && zsrm6;
  @behavior b2:
  @assumes \exists int i; 0 <= i < maxReserves && t == &(reserve_table[i].period_timer);
  @ensures t->stac_armed == 1 && t->stac_handler == kernel_timer_handler;
  @ensures t->stac_expiration_ns == reserve_table[t->rid].period.tv_sec * 1000000000L + reserve_table[t->rid].period.tv_nsec;
*/
/*********************************************************************/
int add_timerq(struct zs_timer *t)
{
  unsigned long long expiration_ns;
  expiration_ns = t->expiration.tv_sec * 1000000000L + t->expiration.tv_nsec;
  t->absolute_expiration_ns = expiration_ns;
  return arm_relative_timer(t);
}

int add_crit_blocked(struct reserve *r){
  r->crit_block_next = NULL;
  if (crit_blockq == NULL){
    crit_blockq = r;
  } else if (crit_blockq->criticality < r->criticality){
    r->crit_block_next = crit_blockq;
    crit_blockq = r;
  } else {
    struct reserve *t=crit_blockq;
    int rsv_visited=0;
    while(rsv_visited <= MAX_RESERVES && t->crit_block_next != NULL && t->crit_block_next->criticality > r->criticality && r->rid != t->rid){
      rsv_visited++;
      t=t->crit_block_next;
    }
    if (rsv_visited > MAX_RESERVES &&  t->crit_block_next != NULL){
      printk("ZSRMMV.add_crit_blocked(): ERROR corrupted crit_block_queue\n");
      return 0;
    }
    if (r->rid == t->rid){
      // trying to add rid that is already addded
      printk("ZSRMMV.add_crit_blocked(): WARNING tied to add rid(%d) already in queue\n",r->rid);
      return 0;
    }
    if (t->crit_block_next == NULL){
      t->crit_block_next = r;
      r->crit_block_next = NULL;
    } else { // t->crit_block_next->criticality <= r->criticality
      r->crit_block_next = t->crit_block_next;
      t->crit_block_next = r;
    }
  }
  return 1;
}

int add_crit_stack(struct reserve *r)
{
  r->crit_next = NULL;
  if (critical_reservesq == NULL){
    critical_reservesq = r;
  } else if (critical_reservesq->criticality < r->criticality){
    r->crit_next = critical_reservesq;
    critical_reservesq = r;
  } else {
    struct reserve *t=critical_reservesq;
    int rsv_visited = 0;
    while(rsv_visited <= MAX_RESERVES && t->crit_next != NULL && t->crit_next->criticality > r->criticality && r->rid != t->rid){
      rsv_visited ++;
      t=t->crit_next;
    }
    if (rsv_visited > MAX_RESERVES && t->crit_next != NULL){
      printk("ZSRMMV.add_crit_stack() ERROR corrupted crit_stack\n");
      return 0;
    }
    if (r->rid == t->rid){
      printk("ZSRMMV.add_crit_stack() WARNING tried to add rid(%d) that was already there\n",r->rid);
      return 0;
    }
    if (t->crit_next == NULL){
      t->crit_next = r;
      r->crit_next = NULL;
    } else { // t->crit_next->criticality <= r->criticality
      r->crit_next = t->crit_next;
      t->crit_next = r;
    }
  }
  return 1;
}

/*********************************************************************/
/*@requires fp11 && fp12 && fp13;
  @requires fp21 && fp22 && fp23;
  @requires fp31 && fp32 && zsrm_lem1 && zsrm1 && zsrm2 && zsrm3 && zsrm4 && zsrm7;
  @requires elem(r);
  @assigns reserve_table[0..(maxReserves-1)],rm_head,rm_queue_size;
  @ensures fp1;
  @ensures fp2;
  @ensures fp31 && fp32 && zsrm_lem1 && zsrm1 && zsrm2 && zsrm3 && zsrm4 && zsrm7;
*/
/*********************************************************************/
int add_rm_queue(struct reserve *r)
{
  // making sure we start with a null next
  r->rm_next = NULL;

  if (rm_head == NULL){
    rm_head = r;
    r->rm_next = NULL;
  } else if (rm_head->period_ns > r->period_ns){
    r->rm_next = rm_head;
    rm_head = r;
  } else {
    struct reserve *t=rm_head;
    int rsv_visited=0;
    /*@loop invariant elem(t) && t->period_ns <= r->period_ns;
      @loop assigns t;*/
    rsv_visited=0;
    while(rsv_visited <= MAX_RESERVES && t->rm_next != NULL && t->rm_next->period_ns <= r->period_ns && t->rid != r->rid){
      rsv_visited++;
      t=t->rm_next;
    }
    if (t->rid == r->rid){
      // reserve already in
      printk("ZSRMMV.add_rm_queue(): WARNING tried to add reserve rid(%d) already in\n",r->rid);
      return 0;
    }

    if (rsv_visited > MAX_RESERVES && t->rm_next != NULL){
      printk("ZSRMMV.add_rm_queue() ERROR rm_queue corrupted\n");
      return 0;
    }

    r->rm_next = t->rm_next;
    t->rm_next = r;

    /* if (t->rm_next == NULL){ */
    /*   t->rm_next = r; */
    /*   r->rm_next = NULL; */
    /* } else { // t->next_prio->priority < r->priority */
    /*   r->rm_next = t->rm_next; */
    /*   t->rm_next = r; */
    /* } */
  }
  rm_queue_size++;
  return 1;
}

/*********************************************************************/
/*@requires fp1 && fp2 && fp31 && fp32 && zsrm1 && zsrm2 && zsrm3 && zsrm4 && zsrm7;
  @requires elem(r);
  @assigns reserve_table[0..(maxReserves-1)],rm_head,rm_queue_size;
  @ensures fp1;
  @ensures fp2;
  @ensures fp31 && fp32 && zsrm1 && zsrm2 && zsrm3 && zsrm4 && zsrm7;
*/
/*********************************************************************/
void del_rm_queue(struct reserve *r)
{
  int rsv_visited=0;

  if (rm_head == NULL) return;
  if (rm_head == r){
    rm_head = r->rm_next;
    rm_queue_size--;
  } else {
    struct reserve *t = rm_head;
    /*@loop invariant elem(t);
      @loop assigns t;*/
    rsv_visited=0;
    while (rsv_visited <= MAX_RESERVES && t->rm_next != NULL && t->rm_next != r){
      t=t->rm_next;
      rsv_visited ++;
    }

    if (rsv_visited > MAX_RESERVES && t->rm_next != NULL && t->rm_next != r){
      printk("ZSRMMV ERROR: del_rm_queue(rid=%d) rsv_visited > MAX_RESERVES\n",r->rid);
    }

    if (t->rm_next == r){
      t->rm_next = r->rm_next;
      r->rm_next=NULL;
      rm_queue_size--;
    }
  }
}

/*********************************************************************/
/*@requires fp1 && fp2 && fp31 && fp32 && zsrm1 && zsrm2 && zsrm3 && zsrm4 && zsrm7;
  @assigns reserve_table[0..(maxReserves-1)];
  @ensures fp1;
  @ensures fp2;
  @ensures fp31 && fp32 && zsrm1 && zsrm2 && zsrm3 && zsrm4 && zsrm7;
*/
//SC: TBD. this is challenging because the priorities are changed
//globally.
/*********************************************************************/
int calculate_rm_priorities(void)
{
#ifndef STAC_FRAMAC_STUBS
  struct reserve *t=rm_head;
  int topprio=MIN_PRIORITY + rm_queue_size;
  int rsv_visited=0;

  if (topprio >= DAEMON_PRIORITY){
    printk("ZSRMMV.calculate_rm_priorities(): WARNING assigning task priority higher than scheduler task priority. Timing cannot be guaranteed\n");
  }

  /*@loop invariant fp1 && fp2 && fp31 && fp32 && zsrm1 && zsrm2 && zsrm3 && zsrm4 && zsrm7;
    @loop assigns reserve_table[0..(maxReserves-1)];
  */
  rsv_visited=0;
  while(rsv_visited <= MAX_RESERVES && t!= NULL){
    rsv_visited++;
    t->priority = topprio--;
    if (t->priority <=0){
      printk("ZSRMMV.calculate_rm_priority(): ERROR assigned an invalid priority(%d) to rid(%d)\n",t->priority,t->rid);
    }
#ifdef __ZS_DEBUG__
    printk("ZSRMMV: calculate_rm_priorities(): assigning priority(%d) to rid(%d)\n",t->priority,t->rid);
#endif
    t=t->rm_next;
  }

  if (rsv_visited > MAX_RESERVES && t != NULL){
    printk("ZSRMMV.calculate_rm_priorities() WARNING: corrupted rmqueue... tried to visit more than MAX_RESERVES\n");
  }
#endif
  return 0;
}

/*********************************************************************/
/*@requires fp1 && fp2 && fp31 && fp32 && zsrm1 && zsrm2 && zsrm3 && zsrm4 && zsrm7;
  @assigns reserve_table[0..(maxReserves-1)];
  @ensures fp1;
  @ensures fp2;
  @ensures fp31 && fp32 && zsrm1 && zsrm2 && zsrm3 && zsrm4 && zsrm7;
*/
/*********************************************************************/
int set_rm_priorities(void)
{
  struct reserve *t=rm_head;
  int rsv_visited=0;

  /*@loop invariant fp1 && fp2 && fp31 && fp32 && elemNull(t);
    @loop assigns task,t,p;*/
  rsv_visited=0;
  while(rsv_visited <= MAX_RESERVES && t!= NULL){
    rsv_visited++;
    if (t->pid >0){
      push_to_activate(t->rid);
    } else {
      // pid == 0 means that it has not been attached
      if (t->pid != 0){
	printk("ZSRMMV.set_rm_priorities() ERROR pid(%d) invalid\n",t->pid);
      } else {
#ifdef __ZS_DEBUG__
	printk("ZSRMMV.set_rm_priorities() reserve rid(%d) not yet attached\n",t->rid);
#endif
      }
    }
    t=t->rm_next;
  }

  if (rsv_visited > MAX_RESERVES && t != NULL){
    printk("ZSRMMV.set_rm_priorities() WARNING: tried to visite more reserves than MAX_RESERVES\n");
  }

  // Activate activator task
  wake_up_process(active_task);

  return 0;
}

// Dio: cannot use floating point in kernel -- disable for now
#if 0
/*********************************************************************/
/*@requires fp1 && fp2 && fp31 && fp32 && zsrm1 && zsrm2 && zsrm3 && zsrm4 && zsrm7;
  @assigns \nothing;
  @ensures fp1;
  @ensures fp2;
  @ensures fp31 && fp32 && zsrm1 && zsrm2 && zsrm3 && zsrm4 && zsrm7;
*/
/*********************************************************************/
/* float compute_total_utilization(void) */
/* { */
/*   float u=0.0f; */
/*   struct reserve *t = rm_head; */
/*   int rsv_visited=0; */

/*   /\*@loop assigns t,u;*\/ */
/*   rsv_visited=0; */
/*   while(rsv_visited <= MAX_RESERVES && t!=NULL){ */
/*     rsv_visited++; */
/*     u = u + ((t->execution_time.tv_sec*1000000000L+t->execution_time.tv_nsec) / (t->period_ns *1.0f)); */
/*     t=t->rm_next; */
/*   } */
/*   if (rsv_visited >MAX_RESERVES && t != NULL){ */
/*     printk("ZSRMMV.compute_total_utilization() ERROR rm_queue corrupted\n"); */
/*   } */
/*   return u; */
/* } */
#endif

/*********************************************************************/
/*@requires fp1 && fp2 && fp31 && fp32 && zsrm1 && zsrm2 && zsrm3 && zsrm4 && zsrm7;
  @requires elem(r);
  @assigns \nothing;
  @ensures fp1;
  @ensures fp2;
  @ensures fp31 && fp32 && zsrm1 && zsrm2 && zsrm3 && zsrm4 && zsrm7;
*/
/*********************************************************************/
/* int admit(struct reserve *r) */
/* { */
/*   //Dio: cannot use floating point in kernel -- disable for now */

/*   /\* */
/*   float u = ((r->execution_time.tv_sec*1000000000L+r->execution_time.tv_nsec) / (r->period_ns *1.0f)); */
/*   float ut =  compute_total_utilization(); */

/*   if (u+ut<=0.69f){ */
/*     printk("admit():total utilization:%f\n",(u+ut)); */
/*     // schedulable */
/*     return 1; */
/*   } */
/*   // not schedulable */
/*   return 0; */
/*   *\/ */

/*   return 1; */
/* } */

/*********************************************************************/
/*@requires fp11 && fp12 && fp13;
  @requires fp21 && fp22 && fp23;
  @requires fp31 && fp32;
  @requires zsrm_lem1 && zsrm3 && zsrm4 && zsrm7;
  @assigns stac_now;
  @ensures stac_now > \old(stac_now);
  @ensures \result == stac_now;
  @ensures fp11 && fp12 && fp13;
  @ensures fp21 && fp22 && fp23;
  @ensures fp31 && fp32;
  @ensures zsrm_lem1 && zsrm2 && zsrm3 && zsrm4 && zsrm7;
  @behavior b1: @assumes zsrm1 && zsrm2; @ensures zsrm1 && zsrm2;
  @behavior b2: @assumes zsrm1_stop1; @ensures zsrm1_stop1;
  @behavior b3: @assumes zsrm1_stop2 && zsrm2; @ensures zsrm1_stop2 && zsrm2;
*/
/*********************************************************************/
unsigned long long get_now_ns()
#ifdef STAC_FRAMAC_STUBS
;
#else
{
  struct timespec ts;

  getnstimeofday(&ts);

  return (((unsigned long long)ts.tv_sec) * 1000000000L) + (unsigned long long) ts.tv_nsec;
}
#endif

int capture_enforcement_signal(int rid, int pid, int signo)
{
  //struct sched_param p;
  //struct task_struct *task;

  if (rid <0 || rid >= MAX_RESERVES){
    printk("ZSRMMV: ERROR in capture_enforcement_signal rid(%d) outside valid range\n",rid);
    return -1;
  }

  /* if (reserve_table[rid].attached){ */
  /*   task = gettask(pid); */
  /*   if (task == NULL){ */
  /*     printk("ZSRMMV: ERROR in capture_enforcement_signal rid(%d) invalid pid(%d)\n",rid,pid); */
  /*     return -2; */
  /*   } */
  /*   p.sched_priority = reserve_table[rid].priority; */
  /*   if (sched_setscheduler(task,SCHED_FIFO,&p)<0){ */
  /*     printk("ZSRMMV: ERROR in capture_enforcement_signal rid(%d) could not assign priority to pid(%d)\n",rid,pid); */
  /*     return -3; */
  /*   } */
  /* } */

  reserve_table[rid].enforcement_signal_captured = (pid < 0)? 0 : 1;
  reserve_table[rid].enforcement_signal_receiver_pid = pid;
  reserve_table[rid].enforcement_signo = signo;
  return 0;
}

int send_budget_enforcement_signal(struct task_struct *task, int signo, int rid){
  struct siginfo info;

  info.si_signo = signo;
  info.si_value.sival_int = rid;
  info.si_errno = 0; // no recovery?
  info.si_code = SI_QUEUE;

  if (send_sig_info(signo, &info, task)<0){
    printk("ZSRMMV: error sending budget enforcement signal\n");
    return -1;
  }

#ifdef __ZS_DEBUG__
  printk("ZSRMMV: budget enforcement signal sent\n");
#endif
  return 0;
}


unsigned long long calculate_start_time(int rid){
  int i;
  unsigned long long start_ticks=0L;
  int eventtype=0;
  unsigned long long now_ticks=0L;

  now_ticks = get_now_ticks();

  if(!hypmtscheduler_dumpdebuglog(&debug_log, &debug_log_buffer_index)){
    printk(KERN_INFO "ZSRMV.budget_enforcement(): dumpdebuglog hypercall API failed\n");
  } else {
    //printk(KERN_INFO "ZSRMV.budget_enforcement(): dumpdebuglog: total entries=%u\n", debug_log_buffer_index);
    for (i = 0; i< debug_log_buffer_index; i++){
      add_trace_record(debug_log[i].hyptask_id, debug_log[i].timestamp, debug_log[i].event_type);
    }
  }

  for (i=0;i<trace_index;i++){
    if (trace_table[i].rid == rid){
      if (trace_table[i].event_type == DEBUG_LOG_EVTTYPE_CREATEHYPTASK_BEFORE ||
	  trace_table[i].event_type == DEBUG_LOG_EVTTYPE_HYPTASKEXEC_BEFORE ){
	start_ticks = trace_table[i].timestamp_ns ;
	eventtype = trace_table[i].event_type;
      }
    }
  }


  if (eventtype != 0){
    if (trace_table[i].event_type == DEBUG_LOG_EVTTYPE_CREATEHYPTASK_BEFORE){
      start_ticks += reserve_table[rid].period_ticks;
    } else if (trace_table[i].event_type == DEBUG_LOG_EVTTYPE_HYPTASKEXEC_BEFORE){
      start_ticks += (reserve_table[rid].period_ticks - reserve_table[rid].hyp_enforcer_instant_ticks);
    }

    i=0;
    // forward the clock up to next period in the future
    while(start_ticks <= now_ticks){
      start_ticks += reserve_table[rid].period_ticks;
      // just for protecting against infinite loop
      if (i++>100){
	printk("ZSRM.calculate_start_time(): POTENTIAL INFINITE LOOP start(%llu) now(%llu) \n",start_ticks, now_ticks);
	break;
      }
    }
  } else {
    printk("ZSRM.calculate_start_time(): NO HYPTASK EVENTS!!\n");
    start_ticks = now_ticks;
  }

  return start_ticks;
}

unsigned long long calculate_hypertask_preemption_time_ticks(int rid, unsigned long long now_ticks){
  int i;
  unsigned long long cumm_hyppreemption_ticks=0L;


  // find first timestamp after rid started
  for (i=0;i<trace_index;i++)
    if (trace_table[i].timestamp_ns >= reserve_table[rid].current_job_activation_ticks &&
	trace_table[i].timestamp_ns < now_ticks &&
	trace_table[i].rid != rid &&
	trace_table[i].event_type == DEBUG_LOG_EVTTYPE_HYPTASKEXEC_BEFORE){
      if (trace_table[i+1].event_type == DEBUG_LOG_EVTTYPE_HYPTASKEXEC_AFTER){
	cumm_hyppreemption_ticks += (trace_table[i+1].timestamp_ns - trace_table[i].timestamp_ns);
	i++;
      }
    }

  return cumm_hyppreemption_ticks;
}

/*********************************************************************/
/*@requires fp1 && fp2 && fp31 && fp32;
  @requires zsrm_lem1 && zsrm2 && zsrm3 && zsrm4 && zsrm7;
  @requires 0 <= rid < maxReserves;
  @assigns reserve_table[0..(maxReserves-1)],readyq,stac_now;
  @ensures stac_now >= \old(stac_now);
  @ensures fp1;
  @ensures fp2;
  @ensures fp31 && fp32;
  @ensures zsrm_lem1 && zsrm2 && zsrm3 && zsrm4 && zsrm7;
  @behavior b1: @assumes zsrm1; @ensures zsrm1;
  @behavior b2: @assumes zsrm1_stop2; @ensures zsrm1_stop2;
*/
/*********************************************************************/
void budget_enforcement(int rid, int request_stop)
{
  struct task_struct *task;
  int i;
  unsigned long long hypertask_preemption_time_ticks=0L;

  enforcement_start_timestamp_ticks = get_now_ticks();

  // double check if we were preempted by a hypertask.
  // If so, adjust the CPU consumption and reprogram the timer

  hypercall_start_timestamp_ticks = enforcement_start_timestamp_ticks;
  if(!hypmtscheduler_dumpdebuglog(&debug_log, &debug_log_buffer_index)){
    printk(KERN_INFO "ZSRMV.budget_enforcement(): dumpdebuglog hypercall API failed\n");
  } else {
    hypercall_end_timestamp_ticks = get_now_ticks();
    cumm_hypercall_ticks += hypercall_end_timestamp_ticks - hypercall_start_timestamp_ticks;
    if (wc_hypercall_ticks < (hypercall_end_timestamp_ticks - hypercall_start_timestamp_ticks)){
      wc_hypercall_ticks = (hypercall_end_timestamp_ticks - hypercall_start_timestamp_ticks);
    }
    num_hypercalls ++;
    for (i = 0; i< debug_log_buffer_index; i++){
      add_trace_record(debug_log[i].hyptask_id, debug_log[i].timestamp, debug_log[i].event_type);
    }

    // build a preemption intersection after we add the trace
    hypertask_preemption_time_ticks = calculate_hypertask_preemption_time_ticks(rid, kernel_entry_timestamp_ticks);

    if (reserve_table[rid].current_job_hypertasks_preemption_ticks < hypertask_preemption_time_ticks){
      reserve_table[rid].current_job_hypertasks_preemption_ticks = hypertask_preemption_time_ticks;
      // update the current_exectime_ticks
      // without taking into account hypertasks preemptions
      reserve_table[rid].current_exectime_ticks += (kernel_entry_timestamp_ticks - reserve_table[rid].start_ticks);
      reserve_table[rid].start_ticks = kernel_entry_timestamp_ticks;

			if (start_enforcement_timer(&(reserve_table[rid])) == 0){
			  // if enough time to defer the timer return otherwise assume that the timer expired and proceed with enforcement
			  return;
			}
    }
  }


  add_trace_record(rid, ticks2ns(kernel_entry_timestamp_ticks), TRACE_EVENT_BUDGET_ENFORCEMENT);//ticks2ns(enforcement_start_timestamp_ticks), TRACE_EVENT_BUDGET_ENFORCEMENT);
#ifdef __ZS_DEBUG__
  printk("ZSRMMV: budget_enforcement(rid(%d)) pid(%d)\n",rid, reserve_table[rid].pid);
#endif

  // cancel the zero_slack timer just in case it is still active
  if (reserve_table[rid].has_zsenforcement){
    hrtimer_cancel(&(reserve_table[rid].zero_slack_timer.kernel_timer));
  }

  reserve_table[rid].num_enforcements++;

  if (reserve_table[rid].enforcement_signal_captured){
    // if the enforcement signal is capture we call the signal handler
    // and then enforce the other task. This can potentially be
    // a thread in the enforced task but this is trusted to finish on time
    // (included in the budget) given that is not supervised by the temporal enforcer

    task = gettask(reserve_table[rid].pid,reserve_table[rid].task_namespace); //enforcement_signal_receiver_pid);
    if (task != NULL){
      /* send_budget_enforcement_signal(task,reserve_table[rid].enforcement_signo, */
      /* 				     BUNDLE_RID_STOP_PERIODIC(rid,request_stop, */
      /* 							      (reserve_table[rid].non_periodic_wait ? 0 : 1) )); */
      send_budget_enforcement_signal(task,reserve_table[rid].enforcement_signo,
				     BUNDLE_RID_STOP_PERIODIC(rid,request_stop,
							      (reserve_table[rid].end_of_period_marked ? 0 : 1) ));
    }
    if (!request_stop){
      add_trace_record(rid, ticks2ns(kernel_entry_timestamp_ticks), TRACE_EVENT_DONT_WFNP);//ticks2ns(enforcement_start_timestamp_ticks), TRACE_EVENT_DONT_WFNP);
    }
  } else {
    // ONLY ENFORCE IF THE SIGNAL IS NOT CAPTURED -- NOT SECURE MUST BE MODIFIED LATER
    // ask the scheduler_thread to stop this thread
    if (request_stop){
      reserve_table[rid].request_stop = 1;
      push_to_reschedule(rid);
      wake_up_process(sched_task);
    }
    //reserve_table[rid].start_period--;
  }
}

void reset_exectime_counters(int rid)
{
  if (reserve_table[rid].worst_exectime_ticks < reserve_table[rid].current_exectime_ticks){
    reserve_table[rid].worst_exectime_ticks = reserve_table[rid].current_exectime_ticks;
  }
  reserve_table[rid].avg_exectime_ticks += reserve_table[rid].current_exectime_ticks;
  reserve_table[rid].avg_exectime_ticks_measurements ++;

  reserve_table[rid].current_exectime_ns = 0;
  reserve_table[rid].current_exectime_ticks = 0;
  reserve_table[rid].in_critical_mode=0;
}

void init_exectime_counters(int rid)
{
  reserve_table[rid].current_exectime_ns = 0;
  reserve_table[rid].current_exectime_ticks = 0;
  reserve_table[rid].in_critical_mode=0;
}

/*********************************************************************/
/*@requires fp11 && fp12 && fp13;
  @requires fp21 && fp22 && fp23;
  @requires fp31 && fp32 && zsrm_lem1 && zsrm1 && zsrm2 && zsrm3 && zsrm4 && zsrm7;
  @requires 0 <= rid < maxReserves;
  @assigns reserve_table[0..(maxReserves-1)],readyq,stac_now;
  @ensures fp1;
  @ensures fp2;
  @ensures fp31 && fp32 && zsrm_lem1 && zsrm1 && zsrm2 && zsrm3 && zsrm4 && zsrm7;
  @ensures \forall int i; 0 <= i < maxReserves ==> i != rid ==>
  reserve_table[i].real_exectime_ns == \old(reserve_table[i].real_exectime_ns);
  @ensures reserve_table[rid].real_exectime_ns == 0;
  @ensures zsrm8(rid);
*/
/*********************************************************************/
void start_of_period(int rid)
{
  struct task_struct *task;

  // overhead measurement
  arrival_start_timestamp_ticks = get_now_ticks();
  context_switch_start_timestamp_ticks = arrival_start_timestamp_ticks;

  reserve_table[rid].current_job_activation_ticks = kernel_entry_timestamp_ticks;
  reserve_table[rid].current_job_hypertasks_preemption_ticks = 0L;

  if (reserve_table[rid].job_completed){
    reserve_table[rid].current_job_deadline_ticks = kernel_entry_timestamp_ticks +
      reserve_table[rid].hyp_enforcer_instant_ticks;

    // if the previous job completed successfuly then we should inform the hypervisor of
    // a new guest job really starting (as just continuing an old job)
    if (!hypmtscheduler_guestjobstart(reserve_table[rid].hyptask_handle)) {
	printk("ZSRMV.start_of_period(): hypmtscheduler_guestjobstart() FAILED\n");
      }
  }
  reserve_table[rid].job_completed = 0;

  //add_trace_record(rid,ticks2ns(arrival_start_timestamp_ticks),TRACE_EVENT_START_PERIOD);

  //-- SC: reset "real execution time"
#ifdef STAC_FRAMAC_STUBS
  reserve_table[rid].real_exectime_ns = 0;
#endif

  // protect against timers with incomplete info/ dead tasks
  if (rid <0 || rid >= MAX_RESERVES){
    printk("ZSRMMV.start_of_period(): WARNING tried to start invalid reserve rid(%d)\n",rid);
    return;
  }

  task = gettask(reserve_table[rid].pid,reserve_table[rid].task_namespace);
  if (task == NULL){
    printk("ZSRMMV.start_of_period(): WARNING tried to start reserve rid(%d) for dead process pid(%d) -- calling delete_reserve()\n",rid,reserve_table[rid].pid);
    delete_reserve(rid);
    return;
  }

  // prevent consecutive start_of_period calls without wfnp
  reserve_table[rid].start_period++;

  if (reserve_table[rid].start_period>1){
    printk("ZSRMMV.start_of_period(): ERROR %d consecutive calls to start of period from rid(%d)\n",reserve_table[rid].start_period,rid);

    // call budget_enforcement + cancel enforcement timer
    hrtimer_cancel(&(reserve_table[rid].enforcement_timer.kernel_timer));
    budget_enforcement(rid, 0);
    //if (in_readyq(rid)){
    prev_calling_stop_from=calling_stop_from;
    calling_stop_from=1;
    stop_stac(rid);
      //}
    reserve_table[rid].start_period--;
  }

  reset_exectime_counters(rid);

  // start timers

#ifdef __ZS_DEBUG__
  printk("zsrm.start_of_period: start of period rid(%d)\n",rid);
#endif

  // cancel the zero_slack timer just in case it is still active
  if (reserve_table[rid].has_zsenforcement){
    hrtimer_cancel(&(reserve_table[rid].zero_slack_timer.kernel_timer));
    add_timerq(&reserve_table[rid].zero_slack_timer);
  }

  // increment the number of jobs activated
  reserve_table[rid].job_activation_count ++;

  /**
   * Now instead of programming the periodic timer at every period, we program it
   * at reserve attachement and just request to be reprogrammed with HRTIMER_RESTART
   */


  /**
   * The periodic timer is programmed within the kernel_timer_handler so it should not
   * be here. Commenting it out
   */

  // add_timerq(&reserve_table[rid].period_timer);

  if (reserve_table[rid].criticality < sys_criticality){
    // should not start given that its criticality is lower
    // than current system level

    printk("zsrm.start_of_period: rid(%d) lower crit than sys-crit\n",rid);

#ifdef __ZS_DEBUG__
    printk("zsrm.start_of_period: rid(%d) lower crit than sys-crit\n",rid);
#endif

    arrival_end_timestamp_ticks = get_now_ticks();
    cumm_blocked_arrival_ticks += arrival_end_timestamp_ticks - arrival_start_timestamp_ticks;
    num_blocked_arrivals++;
    arrival_end_timestamp_ticks = arrival_start_timestamp_ticks=0L;

    return;
  }

  reserve_table[rid].enforced = 0; // outside of enforcement

  // "send" wakeup call to task just in case it has
  // not yet call wait_for_next_period()
  //   --- ASSUMING: atomic increment
  reserve_table[rid].num_period_wakeups++;

  // only accumulate one
  if (reserve_table[rid].num_period_wakeups >1)
    reserve_table[rid].num_period_wakeups = 1;

  if (reserve_table[rid].non_periodic_wait){
    if (reserve_table[rid].num_wait_release>0){
      add_trace_record(rid,ticks2ns(kernel_entry_timestamp_ticks),TRACE_EVENT_START_PERIOD_NON_PERIODIC_WAIT_WAKEUP);//ticks2ns(arrival_start_timestamp_ticks),TRACE_EVENT_START_PERIOD_NON_PERIODIC_WAIT_WAKEUP);
      wake_up_process(task);
      set_tsk_need_resched(task);

      calling_start_from = 1;
      start_stac(rid);
    } else {
      add_trace_record(rid,ticks2ns(kernel_entry_timestamp_ticks),TRACE_EVENT_START_PERIOD_NON_PERIODIC_WAIT_NO_WAKEUP);//ticks2ns(arrival_start_timestamp_ticks),TRACE_EVENT_START_PERIOD_NON_PERIODIC_WAIT_NO_WAKEUP);
    }
    reserve_table[rid].num_wait_release = 0;
  } else {
    add_trace_record(rid,ticks2ns(kernel_entry_timestamp_ticks),TRACE_EVENT_START_PERIOD_PERIODIC_WAIT);//ticks2ns(arrival_start_timestamp_ticks),TRACE_EVENT_START_PERIOD_PERIODIC_WAIT);
      wake_up_process(task);
      set_tsk_need_resched(task);

      calling_start_from = 1;
      start_stac(rid);
  }

  // overhead measurement
  arrival_end_timestamp_ticks = get_now_ticks();
  context_switch_end_timestamp_ticks = arrival_end_timestamp_ticks;

  if (readyq == &reserve_table[rid]){
    // new activated task is now active
    cumm_context_switch_ticks += context_switch_end_timestamp_ticks -
      context_switch_start_timestamp_ticks;
    num_context_switches ++;
    if (wc_context_switch_ticks < (context_switch_end_timestamp_ticks -context_switch_start_timestamp_ticks)){
      wc_context_switch_ticks = (context_switch_end_timestamp_ticks -context_switch_start_timestamp_ticks);
    }
  } else {
    // arrivals without context switch
    cumm_arrival_ticks += arrival_end_timestamp_ticks - arrival_start_timestamp_ticks;
    num_arrivals++;
    if (wc_arrival_ticks < (arrival_end_timestamp_ticks - arrival_start_timestamp_ticks)){
      wc_arrival_ticks = (arrival_end_timestamp_ticks - arrival_start_timestamp_ticks);
    }
  }
  arrival_end_timestamp_ticks = arrival_start_timestamp_ticks = 0L;
  context_switch_end_timestamp_ticks = context_switch_start_timestamp_ticks=0L;
}

void zs_enforcement(int rid)
{
  struct reserve *rsv = readyq;
  int call_scheduler=0;
  int rsv_visited=0;

  zs_enforcement_start_timestamp_ticks = get_now_ticks();

#ifdef __ZS_DEBUG__
  printk("zsrmv.zs_enforcement rid(%d)\n",rid);

#endif
  if (reserve_table[rid].enforced || reserve_table[rid].criticality < sys_criticality){
    zs_enforcement_start_timestamp_ticks = 0L;
#ifdef __ZS_DEBUG__
    printk("zsrm.zs_enforcment rid(%d) is enforced itself\n",rid);
#endif
    return;
  }
  //first set the new sys_criticality
  sys_criticality = reserve_table[rid].criticality;
  reserve_table[rid].in_critical_mode =1;

  add_crit_stack(&reserve_table[rid]);
  rsv_visited = 0;
  while(rsv_visited <= MAX_RESERVES && rsv != NULL){
    rsv_visited ++;
    if (rsv->criticality < sys_criticality){
      struct reserve *r = rsv;
      rsv = rsv->next;
      r->request_stop=1;
      push_to_reschedule(r->rid);
      call_scheduler=1;
      // add to blocked reserves
      add_crit_blocked(r);
#ifdef __ZS_DEBUG__
      printk("zsrmv.zs_enforcement rid(%d) enforcing rid(%d)\n",rid, r->rid);
#endif
    } else {
      rsv = rsv->next;
    }
  }
  if (rsv_visited > MAX_RESERVES && rsv != NULL){
    printk("ZSRMMV.zs_enforcement() ERROR reserve queue corrupted\n");
  }
  if (call_scheduler)
    wake_up_process(sched_task);
}

/*********************************************************************/
/*@requires fp1 && fp2 && fp31 && fp32 && zsrm_lem1 &&
  zsrm1 && zsrm2 && zsrm3 && zsrm4 && zsrm7;
  @requires elemt(timer);
  @assigns reserve_table[0..(maxReserves-1)],readyq,stac_now;
  @ensures fp1;
  @ensures fp2;
  @ensures fp31 && fp32 && zsrm_lem1 && zsrm1 && zsrm2 && zsrm3 && zsrm4 && zsrm7;
*/
/*********************************************************************/
int timer_handler(struct zs_timer *timer)
{
  // returns 1 if HRTIMER should restart, 0 otherwise
  int restart_timer;

  kernel_entry_timestamp_ticks = get_now_ticks();

  restart_timer = 0;

  // process the timer
  //#ifdef __ZS_DEBUG__
  printk("ZSRMMV: timer handler rid(%d) timer-type(%s)\n",timer->rid,STRING_TIMER_TYPE(timer->timer_type));
  //#endif

  if (timer->rid <0 || timer->rid >= MAX_RESERVES ||(reserve_table[timer->rid].pid == -1)){
    printk("ZSRMMV: ERROR timer with invalid reserve rid(%d) or pid\n",timer->rid);
  } else {
    struct task_struct *task;
    task = gettask(reserve_table[timer->rid].pid,reserve_table[timer->rid].task_namespace);
    if (task != NULL){
      switch(timer->timer_type){
      case TIMER_ENF:
	budget_enforcement(timer->rid, 1);
	break;
      case TIMER_PERIOD:
	start_of_period(timer->rid);
	restart_timer = 1;
	break;
      case TIMER_ZS_ENF:
	zs_enforcement(timer->rid);
	break;
      case TIMER_START:
	printk("ZSRMV.timer_handler(): attaching pid(%d) to rid(%d)\n",reserve_table[timer->rid].pid,timer->rid);
	wake_up_process(task);
	set_tsk_need_resched(task);
	attach_reserve(timer->rid,reserve_table[timer->rid].pid);
	break;
      default:
	printk("ZSRMMV: unknown type of timer\n");
	break;
      }
    } else {
      printk("ZSRMMV: timer(%s) without process(%d) -- deleting reserve rid(%d)\n",
	     STRING_TIMER_TYPE(timer->timer_type),
	     reserve_table[timer->rid].pid,
	     timer->rid);

      // make sure that request_stop is not set
      reserve_table[timer->rid].request_stop =0;

      //force a reschedule that will delete the reserve
      push_to_reschedule(timer->rid);
      wake_up_process(sched_task);
    }
  }

  return restart_timer;
}

/*********************************************************************/
/*@requires fp11 && fp12 && fp13;
  @requires fp21 && fp22 && fp23;
  @requires fp31 && fp32;
  @requires zsrm_lem1 && zsrm1 && zsrm2 && zsrm3 && zsrm4 && zsrm7;
  @requires (0 <= rid < maxReserves);
  @assigns reserve_table[0..(maxReserves-1)],readyq,stac_now;
  @ensures fp11 && fp12 && fp13;
  @ensures fp21 && fp22 && fp23;
  @ensures fp31 && fp32;
  @ensures zsrm_lem1 && zsrm1 && zsrm2 && zsrm3 && zsrm4 && zsrm7;
*/
/*********************************************************************/
int attach_reserve(int rid, int pid)
{
  struct task_struct *task;
  struct pid_namespace *ns = task_active_pid_ns(current);
  task = gettask(pid,ns);
  if (task == NULL)
    return -1;

  // mark the starting of the first period
  reserve_table[rid].start_period = 1;

  reserve_table[rid].enforcement_timer.timer_type = TIMER_ENF;
  reserve_table[rid].period_timer.timer_type = TIMER_PERIOD;
  reserve_table[rid].period_timer.expiration.tv_sec = reserve_table[rid].period.tv_sec;
  reserve_table[rid].period_timer.expiration.tv_nsec = reserve_table[rid].period.tv_nsec;
  reserve_table[rid].zero_slack_timer.expiration.tv_sec = reserve_table[rid].zsinstant.tv_sec;
  reserve_table[rid].zero_slack_timer.expiration.tv_nsec = reserve_table[rid].zsinstant.tv_nsec;
  reserve_table[rid].zero_slack_timer.timer_type = TIMER_ZS_ENF;
  reserve_table[rid].start_timer.timer_type = TIMER_START;
  reserve_table[rid].next = NULL;
  reserve_table[rid].enforced=0;
  reserve_table[rid].in_critical_mode=0;
  reserve_table[rid].bound_to_cpu=0; // request activator to bound task to cpu

  init_exectime_counters(rid);
  // MOVED TO ACTIVATOR TASK
/* #if LINUX_VERSION_CODE < KERNEL_VERSION(3,19,0) */
/*   { */
/*     cpumask_t cpumask; */
/*     cpus_clear(cpumask); */
/*     cpu_set(0); */
/*     set_cpus_allowed_ptr(task,&cpumask); */
/*   } */
/* #else */
/*   set_cpus_allowed_ptr(task,cpumask_of(0)); */
/* #endif */

  reserve_table[rid].pid = pid;
  reserve_table[rid].task_namespace = ns;

  //reset_exectime_counters(rid);

  // calculate new priorities of all tasks.
  calculate_rm_priorities();

  // assign new priorities to all tasks
  set_rm_priorities();

  // mark as attached.
  reserve_table[rid].attached = 1;

#ifdef __ZS_DEBUG__
  printk("ZSRMMV: attached rid(%d) to pid(%d)\n",rid, pid);
#endif

  // TODO: should we move this to the activator??
  add_timerq(&(reserve_table[rid].period_timer));

  if (reserve_table[rid].has_zsenforcement){
    add_timerq(&(reserve_table[rid].zero_slack_timer));
  }

  add_trace_record(rid,ticks2ns(kernel_entry_timestamp_ticks),TRACE_EVENT_START_PERIOD);//ticks2ns(get_now_ticks()),TRACE_EVENT_START_PERIOD);

  // start hypertask hrtimer_restart
	create_hypertask(rid);

  // record the first activation
  reserve_table[rid].first_job_activation_ns = ktime_to_ns(ktime_get());// ticks2ns(kernel_entry_timestamp_ticks);
  reserve_table[rid].job_activation_count++;
  reserve_table[rid].current_job_activation_ticks = kernel_entry_timestamp_ticks;
  reserve_table[rid].current_job_deadline_ticks = kernel_entry_timestamp_ticks +
    reserve_table[rid].hyp_enforcer_instant_ticks;
  reserve_table[rid].job_completed=1;

  calling_start_from = 2;
  start_stac(rid);
  return 0;
}

/*********************************************************************/
/*@requires fp1 && fp2 && fp31 && fp32;
  @requires zsrm_lem1 && zsrm1 && zsrm2 && zsrm3 && zsrm4 && zsrm7;
  @requires (0 <= rid < maxReserves);
  @assigns reserve_table[0..(maxReserves-1)],readyq,stac_now;
  @ensures fp1;
  @ensures fp2;
  @ensures fp31 && fp32 && zsrm_lem1 && zsrm1 && zsrm2 && zsrm3 && zsrm4 && zsrm7;
*/
/*********************************************************************/
int delete_reserve(int rid)
{
  struct task_struct *task;
  struct sched_param p;
  int need_enforcement = 0;
  int rsv_visited=0;
  unsigned long long now_ticks = get_now_ticks();

  if (rid <0 || rid >= MAX_RESERVES){
    printk("ZSRMMV: WARNING tried to delete invalid reserve");
    return -1;
  }

  task = gettask(reserve_table[rid].pid,reserve_table[rid].task_namespace);

  /* Special case when the task died and we are forcing
   *   the deletion of the reserve
   */
  if (task != NULL){
    prev_calling_stop_from = calling_stop_from;
    calling_stop_from = 2;
    stop_stac(rid);
  } else if (readyq != NULL) {
    if (readyq != &reserve_table[rid]){
      struct reserve *t = readyq;

      // **** DEBUG READYQ ****
      //printk("ZSRMMV.delete_reserve(%d) accessing readyq\n",rid);

      rsv_visited=0;
      while (rsv_visited <= MAX_RESERVES && t->next != NULL && t->next != &reserve_table[rid]){
	t=t->next;
	rsv_visited++;
      }

      if (rsv_visited > MAX_RESERVES && t->next != NULL){
	printk("ZSRMMV ERROR: delete_reserve(rid=%d) readyq corrupted rsv_visited > MAX_RESERVES\n",rid);
      }

      if (t->next == &reserve_table[rid]){
	t->next = reserve_table[rid].next;
	reserve_table[rid].next = NULL;
      }
    } else if (readyq == &reserve_table[rid]) {

      // *** DEBUG READYQ ***
      //printk("ZSRMMV.delete_reserve(%d) accessing readyq\n",rid);

      readyq = readyq->next;
      reserve_table[rid].next = NULL;
      if (readyq != NULL){
	// double check for existest on task
	task = gettask(readyq->pid,readyq->task_namespace);
	  if (task != NULL){
	    readyq->start_ticks = now_ticks;
	    need_enforcement = start_enforcement_timer(readyq);
	    if (need_enforcement){
#ifdef __ZS_DEBUG__
	      printk("ZSRMMV.delete_reserve(rid=%d) enforcing rid(%d) immediately after activation\n",rid,readyq->rid);
#endif
	      budget_enforcement(readyq->rid,1);
	    }
	  }
	  // else we will let the timer force the deletion.
	  // Note that if the reserve was not properly deleted at least the
	  // periodic timer will be active and we will take advantage of it
	  // to delete te reserve.
      }
    } else {
      printk("ZSRMMV: deleting reserve not in ready queue\n");
    }
  }
  hrtimer_cancel(&(reserve_table[rid].enforcement_timer.kernel_timer));
  hrtimer_cancel(&(reserve_table[rid].period_timer.kernel_timer));

  if (reserve_table[rid].has_zsenforcement){
    hrtimer_cancel(&(reserve_table[rid].zero_slack_timer.kernel_timer));
  }

  if (reserve_table[rid].hypertask_active){
#ifndef __ZSV_SECURE_TASK_BOOTSTRAP__
    if(!hypmtscheduler_deletehyptask(reserve_table[rid].hyptask_handle)){
      printk("ZSRMV.delete_reserve(): error deleting hypertask\n");
    } else {
      reserve_table[rid].hypertask_active=0;
    }
#endif
  }

#ifdef __ZS_DEBUG__
  printk("ZSRMMV.delete reserve(): enforcements(%ld), wfnps(%ld)\n",
	 reserve_table[rid].num_enforcements,
	 reserve_table[rid].num_wfnp);
#endif

  reserve_table[rid].pid=-1;
  reserve_table[rid].next=NULL;
  if (task != NULL){
    p.sched_priority=0;
    sched_setscheduler(task,SCHED_NORMAL,&p);
  }

  del_rm_queue(&reserve_table[rid]);
  return 0;
}

int get_wcet_ns(int rid, unsigned long long *wcet)
{
  *wcet = ticks2ns(reserve_table[rid].worst_exectime_ticks);
  return 0;
}

int get_acet_ns(int rid, unsigned long long *avet)
{
  if (reserve_table[rid].avg_exectime_ticks_measurements>0){
    /* *avet = ticks2ns(reserve_table[rid].avg_exectime_ticks / */
    /* 		     reserve_table[rid].avg_exectime_ticks_measurements); */
    *avet = DIV(reserve_table[rid].avg_exectime_ticks,reserve_table[rid].avg_exectime_ticks_measurements);
  } else {
    *avet = 0L;
  }
  return 0;
}

/*********************************************************************/
//-- does not require zsrm2 but ensures it
/*@requires fp1 && fp2 && fp31 && fp32;
  @requires zsrm_lem1 && zsrm3 && zsrm4 && zsrm7;
  @requires elem(rsvp) && rsvp == readyq;
  @assigns rsvp->enforcement_timer,stac_now;
  @ensures stac_now >= \old(stac_now);
  @ensures fp1;
  @ensures fp2;
  @ensures fp31 && fp32;
  @ensures zsrm_lem1 && zsrm2 && zsrm3 && zsrm4 && zsrm7;
  @behavior b1: @assumes zsrm1; @ensures zsrm1 && zsrm5 && zsrm6;
  @behavior b2: @assumes zsrm1_stop2; @ensures zsrm1_stop2 && zsrm5 && zsrm6;
*/
/*********************************************************************/
int start_enforcement_timer(struct reserve *rsvp)
{
  unsigned long long rest_ticks;
  unsigned long long rest_ns;

  //if (rsvp->current_exectime_ns < rsvp->exectime_ns){
  if (rsvp->exectime_ticks > (rsvp->current_exectime_ticks - rsvp->current_job_hypertasks_preemption_ticks)){
    rest_ticks = rsvp->exectime_ticks -
      (rsvp->current_exectime_ticks - rsvp->current_job_hypertasks_preemption_ticks);
    rest_ns = ticks2ns1(rest_ticks);
    /* printk("ZSRM.start_enforcement_timer():  rid(%d) C(%llu) c(%llu) h(%llu) expires in %llu ns\n", */
    /* 	   rsvp->rid, */
    /* 	   ticks2ns1(rsvp->exectime_ticks), */
    /* 	   ticks2ns1(rsvp->current_exectime_ticks), */
    /* 	   ticks2ns1(rsvp->current_job_hypertasks_preemption_ticks), */
    /* 	   rest_ns); */
    rsvp->enforcement_timer.expiration =  ktime_to_timespec(ns_to_ktime(rest_ns));//ktime_to_timespec(ns_to_ktime(rest_ticks));
    /* rsvp->enforcement_timer.expiration.tv_sec = (rest_ns / 1000000000L); */
    /* rsvp->enforcement_timer.expiration.tv_nsec = (rest_ns % 1000000000L); */
  } else {
    return 1;
  }

#ifdef __ZS_DEBUG__
  printk("ZSRMMV: start_enforcement_timer rid(%d) STARTED\n",rsvp->rid);
#endif
  add_timerq(&(rsvp->enforcement_timer));
  return 0;

  /* } else { */
  /*   stac_exit(); */
  /*   // already expired! */
  /*   // ask whoever is calling me to call enforcement */
  /*   printk("ZSRMMV: start_enforcement_timer FAILED -- already expired\n"); */
  /*   return 1; */
  /* } */
}

/*********************************************************************/
/*@requires fp1 && fp2 && fp31 && fp32;
  @requires zsrm_lem1 && zsrm1 && zsrm2 && zsrm3 && zsrm4 && zsrm7;
  @requires (0 <= rid < maxReserves);
  @assigns reserve_table[0..(maxReserves-1)],readyq,stac_now;
  @ensures fp1;
  @ensures fp21 && fp22 && fp23;
  @ensures fp31 && fp32 && zsrm_lem1 && zsrm1 && zsrm2 && zsrm3 && zsrm4 && zsrm7;
  @ensures readyq != \null ==> zsrm5 && zsrm6;
*/
/*********************************************************************/
void start_stac(int rid)
{
#ifdef STAC_FRAMAC_STUBS
  //-- SC: update "real execution time"
  if(readyq != NULL)
    readyq->real_exectime_ns += get_now_ns() - readyq->real_start_ns;
#endif

  start(rid);

#ifdef STAC_FRAMAC_STUBS
  //-- SC: update "real start_ns"
  if(readyq != NULL)
    readyq->real_start_ns = get_now_ns();
#endif
}

int in_readyq(int rid)
{
  int rsv_visited=0;
  int cnt=0;
  struct reserve *tmp = readyq;
  while (rsv_visited <= MAX_RESERVES && tmp != NULL){// && tmp->rid != rid){
    rsv_visited++;
    if (tmp->rid == rid)
      cnt++;
    tmp = tmp->next;
  }

  if (rsv_visited > MAX_RESERVES && tmp != NULL){
    printk("ZSRMMV.in_readyq(): ERROR: queue larger than MAX_RESERVES\n");
  }

  if (cnt > 1){
    printk("ZSRMMV.in_readyq(): ERROR rid(%d) is %d times in the queue\n",rid,cnt);
  }

  return cnt;
  /* if (tmp != NULL && tmp->rid == rid){ */
  /*   return 1; */
  /* } else { */
  /*   return 0; */
  /* } */
}



/*********************************************************************/
/*@requires fp1 && fp2 && fp31 && fp32;
  @requires zsrm_lem1 && zsrm1_start1 && zsrm3 && zsrm4 && zsrm7;
  @requires (0 <= rid < maxReserves);
  @assigns reserve_table[0..(maxReserves-1)],readyq,stac_now;
  @ensures fp1;
  @ensures fp21 && fp22 && fp23;
  @ensures fp31 && fp32 && zsrm_lem1 && zsrm1_start2 && zsrm2 && zsrm3 && zsrm4 && zsrm7;
  @ensures readyq != \null ==> readyq->start_ns <= stac_now;
  @ensures readyq != \null ==> zsrm5 && zsrm6;
*/
/*********************************************************************/
void start(int rid)
{
  int need_enforcement=0;
  unsigned long long now_ticks = get_now_ticks();
  int new_runner=0;
  int old_rid=-1;
  int cnt=0;

  if ((cnt=in_readyq(rid))){
    printk("ZSRMMV.start(): WARNING trying to add rid(%d) already in ready queue cnt(%d)-- aborting addition\n",rid,cnt);
    printk("ZSRMMV.start():     called start from: %s\n",NAME_START_FROM(calling_start_from));
    return;
  }


  // *** DEBUG READYQ ***
  //printk("ZSRMMV.start(%d) accessing readyq called from(%s)\n",rid,NAME_START_FROM(calling_start_from));

  if (readyq == NULL){
    readyq = &reserve_table[rid];
    new_runner=1;
  } else {
    readyq->stop_ticks = now_ticks;

    //SC: abnormal termination
    if (readyq->current_exectime_ticks + readyq->stop_ticks - readyq->start_ticks > readyq->exectime_ticks){
      stac_exit();
    }
    //SC: abnormal termination
    if (readyq->current_exectime_ticks + readyq->stop_ticks - readyq->start_ticks < readyq->current_exectime_ticks){
      stac_exit();
    }

    readyq->current_exectime_ticks += readyq->stop_ticks - readyq->start_ticks;

    // cancel timer
    hrtimer_cancel(&(readyq->enforcement_timer.kernel_timer));

    if (readyq->priority < reserve_table[rid].priority){
      new_runner = 1;
      old_rid = readyq->rid;
      reserve_table[rid].next = readyq;
      // switch to new task
      readyq=&reserve_table[rid];
    } else {
      int rsv_visited=0;
      struct reserve *tmp = readyq;
      /*@loop invariant elem(tmp) && (tmp->priority >= reserve_table[rid].priority);
        @loop assigns tmp;*/
      //while (tmp->next != NULL && tmp->next->priority >= reserve_table[rid].priority){
      rsv_visited = 0;
      //while (rsv_visited <= MAX_RESERVES && tmp->next != NULL && tmp->next->priority > reserve_table[rid].priority && tmp->rid != rid){
      // Make sure we respect FIFO when same priority
      while (rsv_visited <= MAX_RESERVES && tmp->next != NULL && tmp->next->priority >= reserve_table[rid].priority && tmp->rid != rid){
	rsv_visited++;
        tmp = tmp->next;
      }

      if (tmp->rid == rid){
	// double activation+addition. Abort
	printk("ZSRMMV.start(): WARNING tried to ACTIVE an ALREADY ACTIVE task rid(%d)\n",rid);
	// break here!
	return;
      }

      if (rsv_visited > MAX_RESERVES && tmp->next != NULL){
	tmp = readyq;
	rsv_visited=0;
	while (rsv_visited <= MAX_RESERVES && tmp->next != NULL && tmp->next->priority > reserve_table[rid].priority && tmp->rid != rid){
	  rsv_visited++;
	  printk("ZSRMMV.start(): ERROR tried to search more thabn MAX_RESERVES. rid(%d)\n",tmp->rid);
	  tmp = tmp->next;
	}
	// break here
	return;
      }

      if (tmp->next != NULL){ // tmp->next->priority < reserve_table[rid].priority
        reserve_table[rid].next = tmp->next;
        tmp->next = &reserve_table[rid];
      } else {
	tmp->next = &reserve_table[rid];
	reserve_table[rid].next=NULL;
      }
    }
  }

  if (readyq != NULL) {
    // start readyq accounting
    readyq->start_ticks = now_ticks;
    // start enforcement_timer
    need_enforcement= start_enforcement_timer(readyq);
    if (need_enforcement){
      budget_enforcement(readyq->rid,1);
    } else {
      if (new_runner){
	if (old_rid >= 0){
	  add_trace_record(old_rid,ticks2ns(kernel_entry_timestamp_ticks),TRACE_EVENT_PREEMPTED);//ticks2ns(now_ticks),TRACE_EVENT_PREEMPTED);
	}
	add_trace_record(readyq->rid,ticks2ns(kernel_entry_timestamp_ticks),TRACE_EVENT_RESUMED);//ticks2ns(now_ticks),TRACE_EVENT_RESUMED);
      }
    }
  }

  /* if (need_enforcement){ */
  /*   budget_enforcement(readyq->rid); */
  /* } */
}

/*********************************************************************/
/*@requires fp1 && fp2 && fp31 && fp32;
  @requires zsrm_lem1 && zsrm1 && zsrm2 && zsrm3 && zsrm4 && zsrm7;
  @requires (0 <= rid < maxReserves);
  @assigns reserve_table[0..(maxReserves-1)],readyq,stac_now;
  @ensures fp1;
  @ensures fp2;
  @ensures fp31 && fp32 && zsrm_lem1 && zsrm1 && zsrm2 && zsrm3 && zsrm4 && zsrm7;
  @ensures readyq != \null ==> zsrm5 && zsrm6;
*/
/*********************************************************************/
void stop_stac(int rid)
{
#ifdef STAC_FRAMAC_STUBS
  //-- SC: update "real execution time"
  if(readyq != NULL)
    readyq->real_exectime_ns += get_now_ns() - readyq->real_start_ns;
#endif

  stop(rid);

#ifdef STAC_FRAMAC_STUBS
  //-- SC: update "real start_ns"
  if(readyq != NULL)
    readyq->real_start_ns = get_now_ns();
#endif
}

int inside_stop=0;
/*********************************************************************/
/*@requires fp1 && fp2 && fp31 && fp32;
  @requires zsrm_lem1 && zsrm1_stop1 && zsrm3 && zsrm4 && zsrm7;
  @requires (0 <= rid < maxReserves);
  @assigns reserve_table[0..(maxReserves-1)],readyq,stac_now;
  @ensures fp1;
  @ensures fp2;
  @ensures fp31 && fp32 && zsrm_lem1 && zsrm1_stop2 && zsrm2 && zsrm3 && zsrm4 && zsrm7;
  @ensures readyq != \null ==> readyq->start_ns <= stac_now;
  @ensures readyq != \null ==> zsrm5 && zsrm6;
*/
/*********************************************************************/
void stop(int rid)
{
  unsigned long long now_ticks = get_now_ticks();
  int need_enforcement=0;

  if (!in_readyq(rid)){
    printk("ZSRMMV.stop() WARNING trying to stop rid(%d) not in ready queue from(%s) prev(%s)\n",rid,
	   NAME_STOP_FROM(calling_stop_from),NAME_STOP_FROM(prev_calling_stop_from));
    return;
  }

  // *** DEBUG READYQ ***
  //printk("ZSRMMV.stop(%d) modifying readyq\n",rid);

  inside_stop++;

  // make sure I have a correct rid
  if (rid < 0 || rid >= MAX_RESERVES){
    printk("ZSRMMV.stop(): WARNING tried to stop invalid reserve rid(%d)\n",rid);
    return;
  }

  if (readyq == &reserve_table[rid]){
    add_trace_record(rid,ticks2ns(kernel_entry_timestamp_ticks),TRACE_EVENT_PREEMPTED);//ticks2ns(now_ticks),TRACE_EVENT_PREEMPTED);
    if(readyq != NULL) {
      //readyq->stop_ns = now_ns;
      readyq->stop_ticks = now_ticks;

      //SC: abnormal termination
      //if(readyq->current_exectime_ns + readyq->stop_ns - readyq->start_ns > readyq->exectime_ns)
      if (readyq->current_exectime_ticks + readyq->stop_ticks - readyq->start_ticks > readyq->exectime_ticks)
	stac_exit();

      //SC: abnormal termination
      //if(readyq->current_exectime_ns + readyq->stop_ns - readyq->start_ns < readyq->current_exectime_ns)
      if (readyq->current_exectime_ticks + readyq->stop_ticks - readyq->start_ticks < readyq->current_exectime_ticks)
	stac_exit();

      //readyq->current_exectime_ns += readyq->stop_ns - readyq->start_ns;
      readyq->current_exectime_ticks += readyq->stop_ticks - readyq->start_ticks;
    }

    // cancel timer
    hrtimer_cancel(&(readyq->enforcement_timer.kernel_timer));

    if (readyq == &reserve_table[rid]){
      readyq = readyq->next;
      reserve_table[rid].next = NULL;
    }

    if (readyq != NULL) {
      //readyq->start_ns = now_ns;
      readyq->start_ticks = now_ticks;
      // start enforcement timer
      need_enforcement = start_enforcement_timer(readyq);
      if (need_enforcement){
      	budget_enforcement(readyq->rid,1);
      }
      add_trace_record(readyq->rid,ticks2ns(kernel_entry_timestamp_ticks),TRACE_EVENT_RESUMED);//ticks2ns(now_ticks),TRACE_EVENT_RESUMED);
    }

    /* if (need_enforcement){ */
    /*   budget_enforcement(readyq->rid); */
    /* } */
  } else {
    if (readyq != NULL){
      // it is not at the head (readyq)
      // only remove it from the readyq
      int rsv_visited=0;
      struct reserve *t = readyq;
      while (rsv_visited <= MAX_RESERVES && t->next != NULL && t->next != &reserve_table[rid]){
	rsv_visited++;
	t=t->next;
      }

      if (rsv_visited > MAX_RESERVES && t->next != NULL){
	printk("ZSRMMV.stop(): tried to visit more than MAX_RESERVES\n");
      }

      if (t->next == &reserve_table[rid]){
	t->next = reserve_table[rid].next;
	reserve_table[rid].next = NULL;
      }
    } else {
      reserve_table[rid].next = NULL;
    }
  }

  if (in_readyq(rid)){
    printk("ZSRMMV.stop(): ERROR stopped rid(%d) still in ready queue\n",rid);
  }
  inside_stop--;
}

/*********************************************************************/
/*@requires fp1 && fp2 && fp31 && fp32;
  @requires zsrm_lem1 && zsrm1 && zsrm2 && zsrm3 && zsrm4 && zsrm7;
  @requires 0 <= rid < maxReserves;
  @assigns reserve_table[0..(maxReserves-1)],readyq,stac_now;
  @ensures fp1;
  @ensures fp2;
  @ensures fp31 && fp32 && zsrm_lem1 && zsrm1 && zsrm2 && zsrm3 && zsrm4 && zsrm7;
*/
/*********************************************************************/
int wait_for_next_period(int rid, int nowait, int disableHypertask)
{
  struct task_struct *task;
  int rsv_visited = 0;

  //if (nowait)
  //  return 0;

  reserve_table[rid].job_completed=1;

  departure_start_timestamp_ticks = get_now_ticks();

  add_trace_record(rid, ticks2ns(kernel_entry_timestamp_ticks), TRACE_EVENT_WFNP);//ticks2ns(departure_start_timestamp_ticks), TRACE_EVENT_WFNP);

  if (reserve_table[rid].hypertask_active && disableHypertask){
    // cancel hyper_task
    if(!hypmtscheduler_disablehyptask(reserve_table[rid].hyptask_handle)){
      printk("ZSRMV.wait_next_period(): error calling the hypertask disable\n");
    }
  }

  if (reserve_table[rid].has_zsenforcement){
    hrtimer_cancel(&(reserve_table[rid].zero_slack_timer.kernel_timer));
  }

  // Mark as periodic
  reserve_table[rid].non_periodic_wait=0;

  prev_calling_stop_from=calling_stop_from;
  calling_stop_from=3;
  stop_stac(rid);

  reserve_table[rid].num_wfnp++;

  reserve_table[rid].start_period--;
  //printk("ZSRMMV.wait_for_next_period(%d): decremented start_period. Now: %d\n",rid,reserve_table[rid].start_period);

  if (reserve_table[rid].in_critical_mode){
    reserve_table[rid].in_critical_mode=0;
    if (critical_reservesq == &reserve_table[rid]){
      critical_reservesq = critical_reservesq->crit_next;
    }
    if (critical_reservesq != NULL){
      sys_criticality = critical_reservesq->criticality;
    } else {
      sys_criticality = 0;
    }
    // only re-enable blocked tasks if I did not exceeded my nominal execution time
    if (reserve_table[rid].current_exectime_ticks <= reserve_table[rid].nominal_exectime_ticks){
#ifdef __ZS_DEBUG__
      printk("zsrm.wait_for_next_period(): rid(%d) did no exceed nominal -- waking up blocked reserves\n",rid);
#endif
      // re-enable blocked reserves
      rsv_visited=0;
      while(rsv_visited <= MAX_RESERVES && crit_blockq!= NULL && crit_blockq->criticality >=sys_criticality){
	struct reserve *t = crit_blockq;
	rsv_visited ++;
	crit_blockq = crit_blockq->crit_block_next;
	task = gettask(t->pid,t->task_namespace);
	if (task != NULL){
	  wake_up_process(task);
	  set_tsk_need_resched(task);
	  calling_start_from = 3;
	  start_stac(t->rid);
	} else {
	  delete_reserve(t->rid);
	  printk("zsrmv.wait_for_next_period(): could not find criticality-blocked task pid(%d)\n",t->pid);
	}
	//start_stac(t->rid);
      }
      if (rsv_visited > MAX_RESERVES && crit_blockq != NULL){
	printk("ZSRMMV.wait_for_next_period(rid(%d)) [1] ERROR crit_blockq corrupted\n",rid);
      }
    } else {
      // if I exceeded it take them out of the crit_block queue but do not
      // wake them up -- let their period timers wake them up
#ifdef __ZS_DEBUG__
      printk("zsrm.wait_for_next_period(): rid(%d) EXCEEDED nominal -- not waking blocked reserves\n",rid);
#endif
      rsv_visited = 0;
      while(rsv_visited <= MAX_RESERVES && crit_blockq!= NULL && crit_blockq->criticality >=sys_criticality){
	rsv_visited++;
    	crit_blockq = crit_blockq->crit_block_next;
      }
      if (rsv_visited > MAX_RESERVES && crit_blockq != NULL){
	printk("ZSRMMV.wait_for_next_period(rid(%d)) [2]  ERROR crit_blockq corrupted\n",rid);
      }
    }
  }

  departure_end_timestamp_ticks = get_now_ticks();
  cumm_departure_ticks += departure_end_timestamp_ticks - departure_start_timestamp_ticks;
  num_departures++;
  if (wc_departure_ticks < (departure_end_timestamp_ticks - departure_start_timestamp_ticks)){
    wc_departure_ticks = (departure_end_timestamp_ticks - departure_start_timestamp_ticks);
  }
  departure_end_timestamp_ticks = departure_start_timestamp_ticks = 0L;

  // Only go to sleep if I have not received an unprocessed wakeup from
  // the period timer
  //if (reserve_table[rid].num_period_wakeups <= 0 ){ //&& !nowait){
#ifdef __ZS_DEBUG__
  printk("ZSRMMV: wait_next_period rid(%d) pid(%d) STOP\n",rid, current->pid);
#endif
  set_current_state(TASK_INTERRUPTIBLE);
#ifdef __ZS_DEBUG__
  printk("ZSRMMV: wait_next_period rid(%d) pid(%d) RESUME\n",rid, current->pid);
#endif
  /* } else { */
  /*   printk("ZSRMMV.wait_next_period(): skipping sleeping\n"); */
  /*   // reset wcet & acet counter */
  /*   //reset_exectime_counters(rid); */
  /*   // start again */
  /*   start_stac(rid); */
  /* } */

  // decrement the processed wakeup calls from the
  // periodic timer
  // --- ASSUMING atomic decrement
  reserve_table[rid].num_period_wakeups--;

  return 0;
}

int end_of_period(int rid)
{
  struct task_struct *task;
  int rsv_visited = 0;

  departure_start_timestamp_ticks = get_now_ticks();

  add_trace_record(rid, ticks2ns(kernel_entry_timestamp_ticks), TRACE_EVENT_END_PERIOD);//ticks2ns(departure_start_timestamp_ticks), TRACE_EVENT_END_PERIOD);

  if (reserve_table[rid].has_zsenforcement){
    hrtimer_cancel(&(reserve_table[rid].zero_slack_timer.kernel_timer));
  }

  prev_calling_stop_from=calling_stop_from;
  calling_stop_from=3;
  stop_stac(rid);

  reserve_table[rid].num_wfnp++;

  // Signal scheduler that we are about to call a non-periodic wait
  reserve_table[rid].non_periodic_wait = 1;

  // mark the end of period
  reserve_table[rid].end_of_period_marked = 1;

  reserve_table[rid].start_period--;
  //printk("ZSRMMV.wait_for_next_period(%d): decremented start_period. Now: %d\n",rid,reserve_table[rid].start_period);

  if (reserve_table[rid].in_critical_mode){
    reserve_table[rid].in_critical_mode=0;
    if (critical_reservesq == &reserve_table[rid]){
      critical_reservesq = critical_reservesq->crit_next;
    }
    if (critical_reservesq != NULL){
      sys_criticality = critical_reservesq->criticality;
    } else {
      sys_criticality = 0;
    }
    // only re-enable blocked tasks if I did not exceeded my nominal execution time
    if (reserve_table[rid].current_exectime_ticks <= reserve_table[rid].nominal_exectime_ticks){
#ifdef __ZS_DEBUG__
      printk("zsrm.wait_for_next_period(): rid(%d) did no exceed nominal -- waking up blocked reserves\n",rid);
#endif
      // re-enable blocked reserves
      rsv_visited=0;
      while(rsv_visited <= MAX_RESERVES && crit_blockq!= NULL && crit_blockq->criticality >=sys_criticality){
	struct reserve *t = crit_blockq;
	rsv_visited ++;
	crit_blockq = crit_blockq->crit_block_next;
	task = gettask(t->pid,t->task_namespace);
	if (task != NULL){
	  wake_up_process(task);
	  set_tsk_need_resched(task);
	  calling_start_from = 3;
	  start_stac(t->rid);
	} else {
	  delete_reserve(t->rid);
	  printk("zsrmv.wait_for_next_period(): could not find criticality-blocked task pid(%d)\n",t->pid);
	}
	//start_stac(t->rid);
      }
      if (rsv_visited > MAX_RESERVES && crit_blockq != NULL){
	printk("ZSRMMV.wait_for_next_period(rid(%d)) [1] ERROR crit_blockq corrupted\n",rid);
      }
    } else {
      // if I exceeded it take them out of the crit_block queue but do not
      // wake them up -- let their period timers wake them up
#ifdef __ZS_DEBUG__
      printk("zsrm.wait_for_next_period(): rid(%d) EXCEEDED nominal -- not waking blocked reserves\n",rid);
#endif
      rsv_visited = 0;
      while(rsv_visited <= MAX_RESERVES && crit_blockq!= NULL && crit_blockq->criticality >=sys_criticality){
	rsv_visited++;
    	crit_blockq = crit_blockq->crit_block_next;
      }
      if (rsv_visited > MAX_RESERVES && crit_blockq != NULL){
	printk("ZSRMMV.wait_for_next_period(rid(%d)) [2]  ERROR crit_blockq corrupted\n",rid);
      }
    }
  }

  departure_end_timestamp_ticks = get_now_ticks();
  cumm_departure_ticks += departure_end_timestamp_ticks - departure_start_timestamp_ticks;
  num_departures++;
  if (wc_departure_ticks < (departure_end_timestamp_ticks - departure_start_timestamp_ticks)){
    wc_departure_ticks = (departure_end_timestamp_ticks - departure_start_timestamp_ticks);
  }
  departure_end_timestamp_ticks = departure_start_timestamp_ticks = 0L;

  // This is not a real wakeup yet... this will be registered in the
  // wait_for_next_release
  // reserve_table[rid].num_period_wakeups--;

  return 0;
}

int wait_for_next_release(int rid)
{
  // Only go to sleep if I have not received an unprocessed wakeup from
  // the period timer
  unsigned long long wait_arrival_timestamp_ticks = get_now_ticks();
  //add_trace_record(rid, ticks2ns(wait_arrival_timestamp_ticks), TRACE_EVENT_WAIT_RELEASE);

  if (reserve_table[rid].end_of_period_marked){
    if (reserve_table[rid].num_period_wakeups <= 0 ){
      add_trace_record(rid, ticks2ns(kernel_entry_timestamp_ticks), TRACE_EVENT_WAIT_RELEASE_BLOCKED);//ticks2ns(wait_arrival_timestamp_ticks), TRACE_EVENT_WAIT_RELEASE_BLOCKED);
#ifdef __ZS_DEBUG__
      printk("ZSRMMV: wait_next_period rid(%d) pid(%d) STOP\n",rid, current->pid);
#endif
      set_current_state(TASK_INTERRUPTIBLE);
      reserve_table[rid].num_wait_release++;
#ifdef __ZS_DEBUG__
      printk("ZSRMMV: wait_next_period rid(%d) pid(%d) RESUME\n",rid, current->pid);
#endif
    } else {
      // Continue executing & mark new start
      add_trace_record(rid, ticks2ns(kernel_entry_timestamp_ticks), TRACE_EVENT_WAIT_RELEASE_NOT_BLOCKED);//ticks2ns(wait_arrival_timestamp_ticks), TRACE_EVENT_WAIT_RELEASE_NOT_BLOCKED);
      start_stac(rid);
    }

    // unmark the end of period
    reserve_table[rid].end_of_period_marked=0;

    reserve_table[rid].num_period_wakeups--;

  } else {
    // This means that the enforcer was called and it called wait_for_release() which completed the end_of_period()+wait_for_release() pair.
    // Hence, we now need to call for a full wait_for_next_period() instead.

    wait_for_next_period(rid,0, 1);
  }

  return 0;
}

/*********************************************************************/
//-- this function initializes the scheduler. for zsrm, this means
//-- only initializing the key data structures
/*********************************************************************/
/*@requires zsrm_lem1;
  @assigns readyq, rm_head, reserve_table[0..(maxReserves-1)];
  @ensures readyq == \null && rm_head == \null;
  @ensures \forall integer i; 0 <= i < maxReserves ==>
    (reserve_table[i].pid == -1 &&
    reserve_table[i].rid == i &&
    reserve_table[i].next == \null &&
    reserve_table[i].rm_next == \null &&
    reserve_table[i].period_timer.next == \null &&
    reserve_table[i].enforcement_timer.next == \null &&
    reserve_table[i].enforcement_timer.expiration.tv_sec == 0 &&
    reserve_table[i].enforcement_timer.expiration.tv_nsec == 0 &&
    reserve_table[i].exectime_ns == 0 &&
    reserve_table[i].current_exectime_ns == 0 &&
    reserve_table[i].real_exectime_ns == 0 &&
    reserve_table[i].start_ns == 0 &&
    reserve_table[i].real_start_ns == 0);
  @ensures fp1;
  @ensures fp2;
  @ensures fp31 && fp32 && zsrm_lem1 && zsrm1 && zsrm2 && zsrm3 && zsrm4 && zsrm7;
  @ensures readyq != \null ==> zsrm5 && zsrm6;
*/
/*********************************************************************/
void init(void)
{
  int i;

  readyq=NULL;
  rm_head=NULL;

  /*@loop invariant (\forall integer j;
    0 <= j < i ==> (reserve_table[j].pid == -1 &&
      reserve_table[j].rid == j &&
      reserve_table[j].next == \null &&
      reserve_table[j].rm_next == \null &&
      reserve_table[j].period_timer.next == \null &&
      reserve_table[j].enforcement_timer.next == \null &&
      reserve_table[j].enforcement_timer.expiration.tv_sec == 0 &&
      reserve_table[j].enforcement_timer.expiration.tv_nsec == 0 &&
      reserve_table[j].exectime_ns == 0 &&
      reserve_table[j].current_exectime_ns == 0 &&
      reserve_table[j].real_exectime_ns == 0 &&
      reserve_table[j].start_ns == 0 &&
      reserve_table[j].real_start_ns == 0 &&
      reserve_table[j].period_timer.expiration.tv_sec == reserve_table[j].period.tv_sec &&
      reserve_table[j].period_timer.expiration.tv_nsec == reserve_table[j].period.tv_nsec &&
      reserve_table[j].period_timer.rid == j &&
      reserve_table[j].enforcement_timer.rid == j)) && zsrm_lem1;
    @loop assigns i,reserve_table[0..(maxReserves-1)];*/
  for (i=0;i<MAX_RESERVES;i++) {
    reserve_table[i].first_job_activation_ns=0L;
    reserve_table[i].job_activation_count=0L;
    reserve_table[i].current_job_activation_ticks=0L;
    reserve_table[i].current_job_hypertasks_preemption_ticks=0L;
    reserve_table[i].enforcement_type = ENF_NONE;
    reserve_table[i].task_namespace=NULL;
    reserve_table[i].pid=-1;
    reserve_table[i].rid=i;
    reserve_table[i].next=NULL;
    reserve_table[i].rm_next=NULL;
    reserve_table[i].crit_next=NULL;
    reserve_table[i].crit_block_next=NULL;
    reserve_table[i].period_timer.next = NULL;
    reserve_table[i].enforcement_timer.next = NULL;
    reserve_table[i].enforcement_timer.expiration.tv_sec = 0;
    reserve_table[i].enforcement_timer.expiration.tv_nsec = 0;
    reserve_table[i].exectime_ns = 0;
    reserve_table[i].exectime_ticks = 0;
    reserve_table[i].current_exectime_ns = 0;
    reserve_table[i].current_exectime_ticks = 0;
    reserve_table[i].worst_exectime_ticks = 0;
    reserve_table[i].avg_exectime_ticks=0;
    reserve_table[i].avg_exectime_ticks_measurements=0;
    reserve_table[i].start_ns = 0;
    reserve_table[i].request_stop=0;
    reserve_table[i].period_timer.expiration.tv_sec = reserve_table[i].period.tv_sec;
    reserve_table[i].period_timer.expiration.tv_nsec = reserve_table[i].period.tv_nsec;
    reserve_table[i].period_timer.rid = i;
    reserve_table[i].enforcement_timer.rid = i;
    reserve_table[i].zero_slack_timer.rid = i;
    reserve_table[i].in_critical_mode=0;
    reserve_table[i].enforced=0;
    reserve_table[i].criticality=0;
    reserve_table[i].num_enforcements=0;
    reserve_table[i].num_wfnp=0;
    reserve_table[i].non_periodic_wait = 0;
    reserve_table[i].end_of_period_marked=0;
    reserve_table[i].num_wait_release = 0;
    reserve_table[i].num_period_wakeups=0;
    reserve_table[i].enforcement_signal_captured = 0;
    reserve_table[i].attached=0;
    reserve_table[i].hypertask_active = 0;
#ifdef STAC_FRAMAC_STUBS
    reserve_table[i].real_exectime_ns = 0;
    reserve_table[i].real_start_ns = 0;
#endif
  }
}

void init_reserve(int rid)
{
  reserve_table[rid].first_job_activation_ns=0L;
  reserve_table[rid].job_activation_count=0L;
  reserve_table[rid].current_job_activation_ticks=0L;
  reserve_table[rid].current_job_hypertasks_preemption_ticks=0L;
  reserve_table[rid].task_namespace=NULL;
  reserve_table[rid].num_wfnp=0;
  reserve_table[rid].non_periodic_wait=0;
  reserve_table[rid].end_of_period_marked=0;
  reserve_table[rid].num_wait_release=0;
  reserve_table[rid].num_enforcements=0;
  reserve_table[rid].worst_exectime_ticks=0;
  reserve_table[rid].avg_exectime_ticks=0;
  reserve_table[rid].avg_exectime_ticks_measurements=0;
  reserve_table[rid].num_period_wakeups=0;
  reserve_table[rid].enforcement_type = ENF_NONE;
  reserve_table[rid].enforcement_signal_captured = 0;
  reserve_table[rid].attached=0;
  reserve_table[rid].next=NULL;
  reserve_table[rid].rm_next=NULL;
  reserve_table[rid].crit_next=NULL;
  reserve_table[rid].crit_block_next=NULL;
  reserve_table[rid].period_timer.next = NULL;
  reserve_table[rid].enforcement_timer.next = NULL;
  reserve_table[rid].start_period=0;
  reserve_table[rid].hypertask_active=0;
  reserve_table[rid].has_hyptask=0;
  reserve_table[rid].has_zsenforcement = 0;

  printk("ZSRMV: init_reserve(rid(%d))\n",rid);
  // init timers to make sure we do not crash the kernel
  // if timer operations are called before we arm the timer.

  /* hrtimer_init(&(reserve_table[rid].period_timer.kernel_timer), CLOCK_MONOTONIC_RAW, HRTIMER_MODE_REL); */
  /* hrtimer_init(&(reserve_table[rid].enforcement_timer.kernel_timer), CLOCK_MONOTONIC_RAW, HRTIMER_MODE_REL); */
  /* hrtimer_init(&(reserve_table[rid].zero_slack_timer.kernel_timer), CLOCK_MONOTONIC_RAW, HRTIMER_MODE_REL); */

  hrtimer_init(&(reserve_table[rid].period_timer.kernel_timer), CLOCK_MONOTONIC, HRTIMER_MODE_REL);
  hrtimer_init(&(reserve_table[rid].enforcement_timer.kernel_timer), CLOCK_MONOTONIC, HRTIMER_MODE_REL);
  hrtimer_init(&(reserve_table[rid].zero_slack_timer.kernel_timer), CLOCK_MONOTONIC, HRTIMER_MODE_REL);
#ifdef __ZSV_SECURE_TASK_BOOTSTRAP__
  hrtimer_init(&(reserve_table[rid].start_timer.kernel_timer), CLOCK_MONOTONIC, HRTIMER_MODE_REL);
#endif
}

/*********************************************************************/
/*@requires fp1 && fp2 && fp31 && fp32 && zsrm1 && zsrm2 && zsrm3 && zsrm4;
  @assigns reserve_table[0..(maxReserves-1)];
  @ensures fp1;
  @ensures fp2;
  @ensures fp31 && fp32 && zsrm1 && zsrm2 && zsrm3 && zsrm4;
*/
/*********************************************************************/
int getreserve()
{
  int i=0;

  /*@loop invariant (0 <= i <= maxReserves) && fp1 && fp2 && fp31 && fp32
    && zsrm1 && zsrm2 && zsrm3 && zsrm4;
    @loop assigns i,reserve_table[0..(maxReserves-1)];*/
  for(i=0;i<MAX_RESERVES;i++){
    if(reserve_table[i].pid == -1){
      reserve_table[i].pid = 0;
      init_reserve(i);
      return i;
    }
  }

  return -1;
}

/*********************************************************************/
//-- end of VERIFIED file
/*********************************************************************/

MODULE_AUTHOR("Dionisio de Niz");
MODULE_LICENSE("GPL");

#define DEVICE_NAME "zsrmmv"

/* device number. */
static dev_t dev_id;
static unsigned int dev_major;
static struct class *dev_class = NULL;

/* char device structure. */
static struct cdev c_dev;

static DEFINE_SPINLOCK(zsrmlock);
//static DEFINE_SPINLOCK(reschedule_lock);
static DEFINE_SPINLOCK(activate_lock);
struct semaphore zsrmsem;


struct task_struct *gettask(int pid,  struct pid_namespace *task_namespace){
  struct task_struct *tsk;
  struct pid *pid_struct;

  // Search for the pid in the task namespace
  pid_struct = find_pid_ns(pid,task_namespace);
  tsk = pid_task(pid_struct, PIDTYPE_PID);
  return tsk;
}



struct zs_timer *kernel_timer2zs_timer(struct hrtimer *tmr){
  char *ztmrp = (char *) tmr;
  ztmrp = ztmrp - (((char*) &(((struct zs_timer *)ztmrp)->kernel_timer))-ztmrp);
  return (struct zs_timer *) ztmrp;
}

enum hrtimer_restart kernel_timer_handler(struct hrtimer *ktimer){
  unsigned long flags;
  struct zs_timer *zstimer;
  int tries;
  int locked=0;
  int restart_timer=0;
  enum hrtimer_restart krestart = HRTIMER_NORESTART;
  ktime_t kinterval;
  ktime_t kabsexpiration;
  unsigned long long abs_expiration_ns;

  // *** DEBUGGING ONLY

  tries = 1000000;
  while(tries >0 && !(locked = spin_trylock_irqsave(&zsrmlock,flags)))
    tries--;

  if (!locked){
    zstimer = kernel_timer2zs_timer(ktimer);
    printk("ZSRMMV.kernel_timer_handler(type(%s)) spinlock locked by type(%s) cmd(%s): tied %d times. ABORT!\n",
	   STRING_LOCKER(zstimer->timer_type),
	   STRING_LOCKER(prevlocker),
	   STRING_ZSV_CALL(zsrmcall),
	   1000000);
    return HRTIMER_NORESTART;
  }

  //spin_lock_irqsave(&zsrmlock,flags);

  zstimer = kernel_timer2zs_timer(ktimer);

  prevlocker = zstimer->timer_type;

  if (zstimer != NULL){
    restart_timer = timer_handler(zstimer);
    if (restart_timer){
      zstimer->absolute_expiration_ns = zstimer->expiration.tv_sec * 1000000000L + zstimer->expiration.tv_nsec;
      abs_expiration_ns = reserve_table[zstimer->rid].first_job_activation_ns +
      	(zstimer->absolute_expiration_ns * reserve_table[zstimer->rid].job_activation_count);
      kabsexpiration = ns_to_ktime(abs_expiration_ns);
      hrtimer_start(ktimer, kabsexpiration, HRTIMER_MODE_ABS);
      krestart = HRTIMER_NORESTART;
      /* krestart = HRTIMER_RESTART; */
      /* kinterval = ns_to_ktime(zstimer->absolute_expiration_ns); */
      /* hrtimer_forward_now(ktimer, kinterval); */
    } else {
      krestart = HRTIMER_NORESTART;
    }
  } else {
    printk("ZSRMV: kernel_timer_handler: zstimer == NULL\n");
  }

  spin_unlock_irqrestore(&zsrmlock,flags);
  return krestart; //HRTIMER_NORESTART;
}

int reschedule_stack[MAX_RESERVES];
int top=-1;

int push_to_reschedule(int i){
  int ret;
  //unsigned long flags;
  //spin_lock_irqsave(&reschedule_lock,flags);
  if ((top+1) < MAX_RESERVES){
    top++;
    reschedule_stack[top]=i;
    ret =0;
  } else
    ret=-1;

  //spin_unlock_irqrestore(&reschedule_lock,flags);
  return ret;
}

int pop_to_reschedule(void){
  int ret;
  //unsigned long flags;
  //spin_lock_irqsave(&reschedule_lock,flags);
  if (top >=0){
    ret = reschedule_stack[top];
    top--;
  } else
    ret = -1;
  //spin_unlock_irqrestore(&reschedule_lock,flags);
  return ret;
}


static void scheduler_task(void *a){
  int rid;
  struct sched_param p;
  struct task_struct *task;
  unsigned long flags;
  //int swap_task=0;

  while (!kthread_should_stop()) {
    // prevent concurrent execution with interrupts
    spin_lock_irqsave(&zsrmlock,flags);

    kernel_entry_timestamp_ticks = get_now_ticks();

    prevlocker = SCHED_TASK;
    while ((rid = pop_to_reschedule()) != -1) {
      if (reserve_table[rid].request_stop){
	reserve_table[rid].request_stop = 0;
	if (!reserve_table[rid].enforced){
	  reserve_table[rid].enforced=1;
	  task = gettask(reserve_table[rid].pid,reserve_table[rid].task_namespace);
	  if (task == NULL){
	    delete_reserve(rid);
	    continue;
	  }
#ifdef __ZS_DEBUG__
	  printk("ZSRMMV: sched_task: stopping rsv(%d)\n",rid);
#endif
	  set_task_state(task, TASK_INTERRUPTIBLE);
	  //swap_task=0;
	  set_tsk_need_resched(task);
	  // Dio: try commenting out stop_stac()
	  // for testing... this will mess up the accounting
	  // we should probably double check if this is called recursively:
	  //  - global variable (inside_stop)
	  //  - check variable
	  if (inside_stop){
	    printk("ZSRMMV.scheduler_task(): recursive call to stop() count:%d\n",inside_stop);
	  }
	  reserve_table[rid].start_period--;
	  prev_calling_stop_from=calling_stop_from;
	  calling_stop_from=4;
	  stop_stac(rid);
	} else {
	  printk("ZSRMMV.scheduler_task(): reserve_table[rid(%d)].enforced != 0\n",rid);
	}
      } else {
	p.sched_priority = reserve_table[rid].priority;
	task = gettask(reserve_table[rid].pid,reserve_table[rid].task_namespace);
	if (task == NULL){
	  delete_reserve(rid);
	  continue;
	}
#ifdef __ZS_DEBUG__
	printk("ZSRMMV: sched_task: set prio(%d) for pid(%d)\n",p.sched_priority, reserve_table[rid].pid);
#endif
	if ((task->state & TASK_INTERRUPTIBLE) || (task->state & TASK_UNINTERRUPTIBLE)){
	  wake_up_process(task);
	}
	sched_setscheduler(task, SCHED_FIFO, &p);

	calling_start_from = 4;

	start_stac(rid);
      }
    }

    if (enforcement_start_timestamp_ticks != 0L){
      enforcement_end_timestamp_ticks = get_now_ticks();
      cumm_enforcement_ticks += enforcement_end_timestamp_ticks -
	enforcement_start_timestamp_ticks;
      num_enforcements++;
      if (wc_enforcement_ticks < (enforcement_end_timestamp_ticks - enforcement_start_timestamp_ticks)){
	wc_enforcement_ticks = (enforcement_end_timestamp_ticks - enforcement_start_timestamp_ticks);
      }
      enforcement_end_timestamp_ticks = enforcement_start_timestamp_ticks=0L;
    }

    if (zs_enforcement_start_timestamp_ticks != 0L){
      zs_enforcement_end_timestamp_ticks = get_now_ticks();
      cumm_zs_enforcement_ticks += zs_enforcement_end_timestamp_ticks -
	zs_enforcement_start_timestamp_ticks;
      num_zs_enforcements++;
      zs_enforcement_end_timestamp_ticks = zs_enforcement_start_timestamp_ticks = 0L;
    }

    spin_unlock_irqrestore(&zsrmlock,flags);

    set_current_state(TASK_INTERRUPTIBLE);
    schedule();
  }
}

int activate_stack[MAX_RESERVES];
int activate_top=-1;

int push_to_activate(int i){
  int ret;
  unsigned long flags;
  spin_lock_irqsave(&activate_lock,flags);
  if ((activate_top+1) < MAX_RESERVES){
    activate_top++;
    activate_stack[activate_top]=i;
    ret =0;
  } else
    ret=-1;

  spin_unlock_irqrestore(&activate_lock,flags);
  return ret;
}

int pop_to_activate(void){
  int ret;
  unsigned long flags;
  spin_lock_irqsave(&activate_lock,flags);
  if (activate_top >=0){
    ret = activate_stack[activate_top];
    activate_top--;
  } else
    ret = -1;
  spin_unlock_irqrestore(&activate_lock,flags);
  return ret;
}


/**
 * Separate hypertask creating to be able to experiment
 */

void create_hypertask(int rid)
{
  if (!reserve_table[rid].hypertask_active){
#ifndef __ZSV_SECURE_TASK_BOOTSTRAP__
    if (reserve_table[rid].has_hyptask){
      if(!hypmtscheduler_createhyptask(
				       reserve_table[rid].hyp_enforcer_instant.tv_sec * HYPMTSCHEDULER_TIME_1SEC +
				       (reserve_table[rid].hyp_enforcer_instant.tv_nsec / (1000 * 1000)) * HYPMTSCHEDULER_TIME_1MSEC,

				       reserve_table[rid].period.tv_sec * HYPMTSCHEDULER_TIME_1SEC +
				       (reserve_table[rid].period.tv_nsec / (1000 * 1000)) * HYPMTSCHEDULER_TIME_1MSEC,

				       reserve_table[rid].priority, // priority
				       rid, // 3, // hyptask_id?
				       &(reserve_table[rid].hyptask_handle))){
	printk(KERN_INFO "ZSRMV.activator_task(): hypmtschedulerkmod: create_hyptask failed\n");
      } else {
	if (!hypmtscheduler_guestjobstart(reserve_table[rid].hyptask_handle)) {
	  printk("ZSRMV.activator_task(): hypmtscheduler_guestjobstart() FAILED\n");
	} else {
	  reserve_table[rid].hypertask_active=1;
	  printk("ZSRMV.activator_task(): hyptscheduler_createhyptask() SUCCESSFUL\n");
	}
      }
    }
#else
    reserve_table[rid].hypertask_active=1;
#endif
  }
}

static void activator_task(void *a)
{
  int rid;
  /* int cnt,ret; */
  struct sched_param p;
  struct task_struct *task;

  while (!kthread_should_stop()) {
    // prevent concurrent execution with interrupts
    prevlocker = SCHED_TASK;
    while ((rid = pop_to_activate()) != -1) {
      if (rid <0 || rid >= MAX_RESERVES){
	printk("ZSRMMV.activator_task(): ERROR tried to activate invalid reserve rid(%d)\n",rid);
	continue;
      }
      p.sched_priority = reserve_table[rid].priority;
      task = gettask(reserve_table[rid].pid,reserve_table[rid].task_namespace);
      if (task == NULL){
	continue;
      }

      if (!reserve_table[rid].bound_to_cpu){
	reserve_table[rid].bound_to_cpu = 1;
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,19,0)
	{
	  cpumask_t cpumask;
	  cpus_clear(cpumask);
	  cpu_set(0);
	  set_cpus_allowed_ptr(task,&cpumask);
	}
#else
	set_cpus_allowed_ptr(task,cpumask_of(0));
#endif
      }

      if (sched_setscheduler(task, SCHED_FIFO, &p)<0){
	printk("ZSRMMV.activator_task(): ERROR could not set priority(%d) to pid(%d)\n",p.sched_priority, reserve_table[rid].pid);
      } else {
	printk("ZSRMMV.activator_task(): set priority(%d) to pid(%d)\n",p.sched_priority, reserve_table[rid].pid);
      }

			// We'll try creating the hypertask in the attach_reserve
      //create_hypertask(rid);
    }
    set_current_state(TASK_INTERRUPTIBLE);
    schedule();
  }
}


/**
 *  TICKS TO NS FUNCTIONS
 */

unsigned long long ticksperus=0L;


void set_ticksperus(unsigned long long ticks, unsigned long long nanos){
#ifdef __ZS_USE_SYSTSC__
  ticksperus = rdcntfrq();
  printk("ZSRMV: set_ticksperus(): rdcntfrq(%llu)\n",ticksperus);
  ticksperus = DIV(ticksperus, 1000000UL);
  printk("ZSRMV. ticksperus=%llu\n",ticksperus);
#else

  ticksperus = ticks *1000;
  //ticksperus = ticks *10;
  ticksperus = DIV(ticksperus,nanos);
  printk("ZSRMV: ticksperus: ticks(%llu) nanos(%llu) ticksperus(%llu)\n",ticks, nanos, ticksperus);
  /* ticksperus = ticksperus / nanos; */
#endif
}

unsigned long long ticks2ns(unsigned long long ticks)
{
  return ticks; //ticks2ns1(ticks);
}

/* unsigned long long ticks2ns1(unsigned long long ticks) */
/* #if LINUX_VERSION_CODE > KERNEL_VERSION(4,4,6) */
/* { */
/*   return ticks; */
/* } */
/* #else */


unsigned long long ticks2ns1(unsigned long long ticks)
{

/* #ifdef __ZS_USE_TSC__   */
#ifdef __ZS_USE_SYSTSC__
  unsigned long long ns;

  ns = ticks*1000;
  //ns = ticks*10;
  ns = DIV(ns,ticksperus);
  //ns = ns / ticksperus;

  return ns;
#else
  return ticks;
#endif

}
/* #endif */

unsigned long long ns2ticks(unsigned long long ns)
/* #if LINUX_VERSION_CODE > KERNEL_VERSION(4,4,6) */
/* { */
/*   return ns; */
/* } */
/* #else */
{

  //#ifdef __ZS_USE_TSC__
#ifdef __ZS_USE_SYSTSC__

  unsigned long long ticks;

  ticks = ns * ticksperus;
  ticks = DIV(ticks,1000);
  //ticks = DIV(ticks,10);
  //ticks = ticks / 1000;

  return ticks;
#else
  return ns;
#endif
}
/* #endif */

void setup_ticksclock(void)
#ifdef STAC_FRAMAC_STUBS
;
#else
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,4,6)
{}
#else
#if LINUX_VERSION_CODE > KERNEL_VERSION(4,4,6)
{

  //#ifdef __ZS_USE_TSC__
#ifdef __ZS_USE_SYSTSC__
  u64 start_cycles,end_cycles;
  unsigned long long start_ns,i,end_ns;

  //hypmtscheduler_getrawtick64(&start_cycles);
  // start_cycles = rdtsc64();
  start_cycles = sysreg_read_cntpct();

  start_ns = get_now_ns();

  msleep(100);

  /* while (i<10000000){ */
  /*   i = start_ns + i +1;//get_now_ns() - start_ns; */
  /* } */


  end_ns = get_now_ns();

  //hypmtscheduler_getrawtick64(&end_cycles);
  //end_cycles = rdtsc64();
  end_cycles = sysreg_read_cntpct();

  set_ticksperus(end_cycles-start_cycles,end_ns-start_ns);
#endif

}
#else
{
  struct system_time_snapshot snap,snap1;
  unsigned long long i,start;

  ktime_get_snapshot(&snap);
  i=0;
  start = get_now_ns();
  while (i<10000){
    i = get_now_ns() - start;
  }
  ktime_get_snapshot(&snap1);
  set_ticksperus((snap1.cycles-snap.cycles),(snap1.raw.tv64-snap.raw.tv64));
}
#endif
#endif
#endif


/*********************************************************************/
/*@requires fp11 && fp12 && fp13;
  @requires fp21 && fp22 && fp23;
  @requires fp31 && fp32;
  @requires zsrm_lem1 && zsrm3 && zsrm4 && zsrm7;
  @assigns stac_now;
  @ensures stac_now > \old(stac_now);
  @ensures \result == stac_now;
  @ensures fp11 && fp12 && fp13;
  @ensures fp21 && fp22 && fp23;
  @ensures fp31 && fp32;
  @ensures zsrm_lem1 && zsrm2 && zsrm3 && zsrm4 && zsrm7;
  @behavior b1: @assumes zsrm1 && zsrm2; @ensures zsrm1 && zsrm2;
  @behavior b2: @assumes zsrm1_stop1; @ensures zsrm1_stop1;
  @behavior b3: @assumes zsrm1_stop2 && zsrm2; @ensures zsrm1_stop2 && zsrm2;
*/
/*********************************************************************/


unsigned long long get_now_ticks(void)
#ifdef STAC_FRAMAC_STUBS
;
#else
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,4,6)
{ return 0; }
#else
#if LINUX_VERSION_CODE > KERNEL_VERSION(4,4,6)
{

  //#ifdef __ZS_USE_TSC__
#ifdef __ZS_USE_SYSTSC__

  u64 t;
  t= sysreg_read_cntpct();
  //t = rdtsc64();
  return t;
#else

  ktime_t t;
  t =ktime_get_raw();
  return (unsigned long long) ktime_to_ns(t);
#endif
}
#else
{
  struct system_time_snapshot snap;
  ktime_get_snapshot(&snap);
  return snap.cycles;
}
#endif
#endif
#endif


/* dummy function. */
static int zsrm_open(struct inode *inode, struct file *filp)
{
  return 0;
}

/* dummy function. */
static int zsrm_release(struct inode *inode, struct file *filp)
{
  return 0;
}


int test_reserve(int option)
{
    static uint32_t hyptask_handle1;
    static uint32_t hyptask_handle2;

    switch (option){
    case 0:
    	if(!hypmtscheduler_createhyptask(1 * HYPMTSCHEDULER_TIME_1SEC, // first time
				       //+X * HYPMTSCHEDULER_TIME_1USEC,
				       2 * HYPMTSCHEDULER_TIME_1SEC, // sticky period
				       //+ (reserve_table[rid].period.tv_nsec / 1000) * HYPMTSCHEDULER_TIME_1USEC,
				   10, // priority
				       0, //  hyptask_id
				       &hyptask_handle1)){
	printk(KERN_INFO "ZSRMV.test_reserve(): hypmtschedulerkmod: create_hyptask1 failed\n");
      } else {
	printk(KERN_INFO "ZSRMV.test_reserve(): hyptask1 created\n");
      }

    	if(!hypmtscheduler_createhyptask(//1 * HYPMTSCHEDULER_TIME_1SEC, // first time
				       //500000 * HYPMTSCHEDULER_TIME_1USEC,
    		  	  	  (0.5 * HYPMTSCHEDULER_TIME_1SEC),
				       1 * HYPMTSCHEDULER_TIME_1SEC, // sticky period
				       //+ (reserve_table[rid].period.tv_nsec / 1000) * HYPMTSCHEDULER_TIME_1USEC,
				       11, // priority
				       1, //  hyptask_id
				       &hyptask_handle2)){
	printk(KERN_INFO "ZSRMV.test_reserve(): hypmtschedulerkmod: create_hyptask2 failed\n");
      } else {
	printk(KERN_INFO "ZSRMV.test_reserve(): hyptask2 created\n");
      }

      break;
    case 1:
    	if(!hypmtscheduler_deletehyptask(hyptask_handle1)){
	  printk("ZSRMV.test_reserve(): error deleting hypertask1\n");
	} else {
	  printk("ZSRMV.test_reserve(): hypertask1 deleted\n");
	}

#if 1

	if(!hypmtscheduler_deletehyptask(hyptask_handle2)){
	  printk("ZSRMV.test_reserve(): error deleting hypertask2\n");
	} else {
	  printk("ZSRMV.test_reserve(): hypertask2 deleted\n");
	}
#endif

      break;
    default:
	printk("ZSRMV.test_reserve(): wrong option\n");
	break;
    }

  return 0;
}


#define SERIAL_RECEIVING_BUFFER_SIZE 2048

struct semaphore serial_buffer_sem;

u8 serial_receiving_buffer[SERIAL_RECEIVING_BUFFER_SIZE];

#define SERIAL_RECEIVING_CIRCULAR_INC(a) ( (a==SERIAL_RECEIVING_BUFFER_SIZE-1) ? 0 : a+1)

static DECLARE_WAIT_QUEUE_HEAD(serial_read_wait_queue);

// point to next byte to read
volatile int serial_receiving_reading_index=0;

// point to next byte to write
volatile int serial_receiving_writing_index=0;
volatile int waiting_for_input=0;

int serial_data_dropped=0;

int serial_receiving_buffer_write(u8 *buffer, int len)
{
  static int reported_full=0;
  static int invocation_cnt=0;
  int wrote=0;
  int i=0;
  int wasempty=0;

  invocation_cnt++;
  down_interruptible(&serial_buffer_sem);

  wasempty = (serial_receiving_writing_index == serial_receiving_reading_index);

  while(SERIAL_RECEIVING_CIRCULAR_INC(serial_receiving_writing_index) != serial_receiving_reading_index && i<len){
    serial_receiving_buffer[serial_receiving_writing_index] = buffer[i];
    serial_receiving_writing_index = SERIAL_RECEIVING_CIRCULAR_INC(serial_receiving_writing_index);
    i++;
    wrote++;
  }

  // Were there bytes I could not write?
  if (SERIAL_RECEIVING_CIRCULAR_INC(serial_receiving_writing_index) == serial_receiving_reading_index && i<len){
    serial_data_dropped=1;
    if (!reported_full){
      printk("ZSRM.serial_receiving_buffer_write(): invocation(%d) len(%d) buffer full dropped %d bytes\n",invocation_cnt, len, (len-i));
      reported_full=1;
    }
  }

  // Try always sending the wakeup
  //if (wasempty){
  wake_up_interruptible(&serial_read_wait_queue);
  //printk("ZSRM.serial_receiving_buffer_write(): wasempty SENT wakeup\n");
  //}

  up(&serial_buffer_sem);

  return wrote;
}

int serial_receiving_buffer_read(u8 *buffer, int len)
{
  int i=0;
  int read=0;

  down_interruptible(&serial_buffer_sem);

  if (serial_receiving_writing_index == serial_receiving_reading_index){
    up(&serial_buffer_sem);
    //printk("ZSRM.serial_receiving_buffer_read(): EMPTY going to sleep\n");
    SERIAL_DEBUG_FLAG_ON(SERIAL_FLAG_RCV_READ_BLOCKED);
    wait_event_interruptible(serial_read_wait_queue,
    			     (serial_receiving_writing_index != serial_receiving_reading_index ));
    SERIAL_DEBUG_FLAG_OFF(SERIAL_FLAG_RCV_READ_BLOCKED);
    down_interruptible(&serial_buffer_sem);
  }

  while(serial_receiving_writing_index != serial_receiving_reading_index && i<len){
    buffer[i] = serial_receiving_buffer[serial_receiving_reading_index];
    serial_receiving_reading_index = SERIAL_RECEIVING_CIRCULAR_INC(serial_receiving_reading_index);
    i++;
    read++;
  }

  up(&serial_buffer_sem);

  return read;
}

struct hrtimer serial_receiver_kernel_timer;

unsigned long long serial_timer_prev_timestamp_ticks=0L;
unsigned long long serial_timer_largest_elapsed_ticks=0L;
unsigned long long serial_timer_period_ns;

enum hrtimer_restart serial_kernel_timer_handler(struct hrtimer *ktimer){
  ktime_t ktime;

  /* unsigned long long timestamp_ticks; */
  /* unsigned long long elapsed_ticks; */
  /* timestamp_ticks = get_now_ticks(); */

  /* elapsed_ticks = timestamp_ticks - serial_timer_prev_timestamp_ticks; */

  /* if (serial_timer_largest_elapsed_ticks < elapsed_ticks){ */
  /*   serial_timer_largest_elapsed_ticks = elapsed_ticks; */
  /* } */

  wake_up_process(serial_recv_task);
  set_tsk_need_resched(serial_recv_task);

  ktime = ns_to_ktime(serial_timer_period_ns);
  hrtimer_forward_now(ktimer, ktime);
  return HRTIMER_RESTART;
}

static void serial_receiver_task(void *a)
{
  int rid;
  /* int cnt,ret; */
  struct sched_param p;
  struct task_struct *task;
  u8 buffer[100];
  u32 len_read;
  bool readbufferexhausted=false;
  int wasempty=0;

  printk("ZSRMV.serial_receiver_task() READY!\n");

  while (!kthread_should_stop()) {
    len_read=0;
#ifdef __SERIAL_HARDWARE_CONTROL_FLOW__
    serial_stop_transmission();
    SERIAL_DEBUG_FLAG_ON(SERIAL_FLAG_HWR_SND_BLOCKED);
#endif
    do {
      readbufferexhausted=false;
      if(mavlinkserhb_recv(&buffer, sizeof(buffer), &len_read, &readbufferexhausted)){
	if (len_read >0){
	  serial_receiving_buffer_write(buffer,len_read);
	} else {
	  //readbufferexhausted=true;
	}
      } else {
	readbufferexhausted=true;
	printk("ZSRM.serial_receiver_task(): ERROR in mavlinkserhb_recv()\n");
	serial_receiving_error_count++;
      }
      serial_debug_last_receive_count = len_read;
      if (serial_debug_largest_read_count < len_read){
	serial_debug_largest_read_count = len_read;
      }
      if (len_read != 0){
	serial_debug_last_non_zero_receive_count = len_read;
	serial_debug_num_zero_receive_counts=0L;
      } else {
	serial_debug_num_zero_receive_counts++;
      }
    } while(!readbufferexhausted || kthread_should_stop());
#ifdef __SERIAL_HARDWARE_CONTROL_FLOW__
    serial_resume_transmission();
    SERIAL_DEBUG_FLAG_OFF(SERIAL_FLAG_HWR_SND_BLOCKED);
#endif
    serial_debug_before_sleep_timestamp_ticks = get_now_ticks();

    if (!kthread_should_stop()){
      usleep_range(290,300);
    }

    /* set_current_state(TASK_INTERRUPTIBLE); */
    /* schedule(); */


    serial_debug_after_sleep_timestamp_ticks = get_now_ticks();
    serial_debug_sleep_elapsed_interval_ticks = (serial_debug_after_sleep_timestamp_ticks - serial_debug_before_sleep_timestamp_ticks);
    if(serial_debug_max_sleep_ticks < serial_debug_sleep_elapsed_interval_ticks){
      serial_debug_max_sleep_ticks = serial_debug_sleep_elapsed_interval_ticks;
    }
  }

  printk("ZSRMV.serial_receiver_task() EXITING\n");
}


#define SERIAL_SENDING_BUFFER_SIZE 1024

struct semaphore serial_sending_buffer_sem;

u8 serial_sending_buffer[SERIAL_SENDING_BUFFER_SIZE];

#define SERIAL_SENDING_CIRCULAR_INC(a) ( (a == SERIAL_SENDING_BUFFER_SIZE-1) ? 0 : a+1)

static DECLARE_WAIT_QUEUE_HEAD(serial_write_wait_queue);

// point ot next byte to read
volatile int serial_sending_reading_index=0;

// point to next byte to write
volatile int serial_sending_writing_index=0;

int serial_sending_buffer_write(u8 *buffer, int len){
  int was_empty=0;
  int i=0;
  int wrote=0;

  down_interruptible(&serial_sending_buffer_sem);

  was_empty = (serial_sending_writing_index == serial_sending_reading_index);

  while(SERIAL_SENDING_CIRCULAR_INC(serial_sending_writing_index) != serial_sending_reading_index && i < len){
    serial_sending_buffer[serial_sending_writing_index] = buffer[i];
    serial_sending_writing_index = SERIAL_SENDING_CIRCULAR_INC(serial_sending_writing_index);
    i++;
    wrote++;
  }

  if (SERIAL_SENDING_CIRCULAR_INC(serial_sending_writing_index) == serial_sending_reading_index && i<len){
    printk("ZSRM.serial_sending_buffer_write(): dropped data\n");
  }

  //if (was_empty){
    // wakeup transmiter kernel task
  wake_up_process(serial_sender_task);
  //wake_up_interruptible(&serial_write_wait_queue);
   //}

  up(&serial_sending_buffer_sem);

  return wrote;
}

int serial_sending_buffer_read(u8 *buffer, int len){
  int i=0;
  int read=0;

  down_interruptible(&serial_sending_buffer_sem);

  if (serial_sending_writing_index == serial_sending_reading_index){
    up(&serial_sending_buffer_sem);
    /* wait_event_interruptible(serial_write_wait_queue,  */
    /* 			     (serial_sending_writing_index != serial_sending_reading_index )); */

    /* down_interruptible(&serial_sending_buffer_sem); */
    // if sending task receives zero it should go to sleep
    return 0;
  }

  while(serial_sending_writing_index != serial_sending_reading_index && i<len){
    buffer[i] = serial_sending_buffer[serial_sending_reading_index];
    serial_sending_reading_index = SERIAL_SENDING_CIRCULAR_INC(serial_sending_reading_index);
    i++;
    read++;
  }

  up(&serial_sending_buffer_sem);

  return read;

}

static int sending_task_active=1;

static void serial_sending_task(void *a){
  int read=0;
  u8  buffer[100];
  int sending_index=0;
  int batch_size;
  int ret;

  while(!kthread_should_stop()){
    while(sending_task_active && !kthread_should_stop()){
      sending_index=0;
      if ((read = serial_sending_buffer_read(buffer, 100)) > 0){
	while(sending_index < read && !kthread_should_stop()){
	  batch_size = (read-sending_index > 8 ) ? 8 : (read-sending_index);
	  while(serial_is_reception_stopped() && !kthread_should_stop()){
	    usleep_range(590,600);
	  }
	  ret = mavlinkserhb_send(&buffer[sending_index],batch_size);
	  sending_index += batch_size;
	}
      } else {
	// go to sleep
	SERIAL_DEBUG_FLAG_ON(SERIAL_FLAG_SND_READ_BLOCKED);
	set_current_state(TASK_INTERRUPTIBLE);
	schedule();
	SERIAL_DEBUG_FLAG_OFF(SERIAL_FLAG_SND_READ_BLOCKED);
      }
    }
  }
}

uint32_t hyp_serial_recv_task_handle=-1;
int serial_recv_task_running=0;

int init_serial(u32 bauds)
{
  mavlinkserhb_initialize(bauds);

  /* if (!mavlinkserhb_activatehbhyptask(300 * HYPMTSCHEDULER_TIME_1USEC, 300 * HYPMTSCHEDULER_TIME_1USEC, 10)){ */
  /*   printk("ZSRM.init_serial(): error starting serial hyptask\n"); */
  /* } */
  /* if(!hypmtscheduler_createhyptask(300 * HYPMTSCHEDULER_TIME_1USEC, // FIRST TIMER */
  /* 				   300 * HYPMTSCHEDULER_TIME_1USEC, // FOLLOW UP TIMERS */
  /* 				   10, // PRIORITY */
  /* 				   3,  // hypertask id */
  /* 				   &hyp_serial_recv_task_handle // saving the handle */
  /* 				   )){ */
  /*   printk("ZSRM.init_serial(): error creating serial receiving hypertask\n"); */
  /* } else { */
  /*   serial_recv_task_running = 1; */
  /* } */

  return 0;
}

int send(int rid, void *buffer, int buf_len, int finished)
{
  int ret;

  if (finished){
    if (kernel_entry_timestamp_ticks <= reserve_table[rid].current_job_deadline_ticks){
      //ret = mavlinkserhb_send(buffer,buf_len);
      ret = (serial_sending_buffer_write(buffer, buf_len)==buf_len) ?1 : 0;
      wait_for_next_period(rid,
			   0,
			   1 // disableHypertask -- normal actuation sent on time
			   );
    } else {
      // return error: too late to send
      ret = -2;
      wait_for_next_period(rid,
			   0,
			   0 // do not disable hypertask -- if normal actuation was not sent then allow hypertask to send default safe actuation
			   );
    }
  } else {
    //ret = mavlinkserhb_send(buffer,buf_len);
    ret = (serial_sending_buffer_write(buffer,buf_len) == buf_len) ? 1: 0;
  }

  return ret;
}

int receive(int rid, u8 *buffer, int buf_len, unsigned long *flags)
{
  int ret=0;

  /* ret = serial_receiving_buffer_read(buffer,buf_len); */

  /* if (ret == 0){ */

    // free semaphores and re-enable interrupts
    // enable interrupts
    spin_unlock_irqrestore(&zsrmlock, *flags);

    // enable other syscalls
    up(&zsrmsem);


    /**
     *  TODO: For multithreaded use this wait needs to be in a loop until the read from the buffer returns non-zero
     *        We defer this modification until after the demos
     */
    /* wait_event_interruptible(serial_read_wait_queue, */
    /* 			     (serial_receiving_writing_index != serial_receiving_reading_index )); */
    // re-acquire semaphore and disable interrutps


    ret = serial_receiving_buffer_read(buffer,buf_len);

    if (down_interruptible(&zsrmsem) < 0){
      printk("ZSRMV.receive(): could not re-acquire semaphore after sleep\n");
    }

    // disable interrupts to avoid concurrent interrupts
    spin_lock_irqsave(&zsrmlock, *flags);

    /* ret = serial_receiving_buffer_read(buffer,buf_len); */
  /* } */

  return ret;
}

/**
 * This is to simulate a kernel (and VM) error just to test the resilience of the hypervisor
 */

static void simulate_crash(void)
{
  char *argv[] = {"/bin/sync", NULL};
  char *envp[] = {"HOME=/", "TERM=linux", "PATH=/sbin:/bin:/usr/sbin:/usr/bin", NULL};
  call_usermodehelper(argv[0],argv,envp, UMH_WAIT_PROC);
  panic("Simulated Crash");
  printk("ZSRM.simulate_crash() FAILED. call to panic() returned\n");
}


static ssize_t zsrm_read(struct file *filp,	/* see include/linux/fs.h   */
			   char *buffer,	/* buffer to fill with data */
			   size_t length,	/* length of the buffer     */
			   loff_t * offset)
{
  int transfer_size;
  int i;

  printk(KERN_INFO "ZSRMV: dumptrace: trace_index=%u\n", trace_index);

  //zero-initialize debug_log
  memset(&debug_log, 0, sizeof(debug_log));

  // Copy the hypervisor log into the zsrm trace log
  if(!hypmtscheduler_dumpdebuglog(&debug_log, &debug_log_buffer_index)){
    printk(KERN_INFO "ZSRMV: dumpdebuglog hypercall API failed\n");
  } else {
    printk(KERN_INFO "ZSRMV: dumpdebuglog: total entries=%u\n", debug_log_buffer_index);
	for (i = 0; i< debug_log_buffer_index; i++){
      add_trace_record(debug_log[i].hyptask_id, debug_log[i].timestamp, debug_log[i].event_type);
    }
  }

  printk(KERN_INFO "ZSRMV: dumptrace: trace_index=%u\n", trace_index);

  transfer_size = (length >= (trace_index * sizeof(struct trace_rec_t))) ?
    (trace_index * sizeof(struct trace_rec_t)) :
    (length / sizeof(struct trace_rec_t)) * sizeof(struct trace_rec_t) ;

  if (copy_to_user(buffer, trace_table, transfer_size)<0){
      printk(KERN_WARNING "ZSRMV: error copying trace_table to user space\n");
  }

  trace_index = 0;

  return transfer_size;
}

int valid_rid(int rid)
{
  if (rid <0 || rid >= MAX_RESERVES)
    return 0;

  return 1;
}

int active_rid(int rid)
{
  if (valid_rid(rid))
    if (reserve_table[rid].pid != -1)
      return 1;
  return 0;
}

static ssize_t zsrm_write
(struct file *file, const char *buf, size_t count, loff_t *offset)
{
  //int err;
  int need_reschedule=0;
  int ret = 0;
  struct api_call call;
  unsigned long flags;
  unsigned long long wcet;
  unsigned long long Z;

  /* copy data to kernel buffer. */
  if (copy_from_user(&call, buf, count)) {
    printk(KERN_WARNING "ZSRMMV: failed to copy data.\n");
    return -EFAULT;
  }

  // try to lock semaphore to prevent concurrent syscalls
  // before disabling interrupts
  if ((ret = down_interruptible(&zsrmsem)) < 0){
    return ret;
  }

  // disable interrupts to avoid concurrent interrupts
  spin_lock_irqsave(&zsrmlock,flags);

  kernel_entry_timestamp_ticks = get_now_ticks();


  // *** DEBUG LOCKER
  prevlocker = ZSV_CALL;
  zsrmcall = call.cmd;

  switch (call.cmd) {
  case SIM_CRASH:
#ifdef ZSV_SIMULATE_CRASH
    simulate_crash();
    ret = 0;
#else
    ret = -1;
#endif
    break;
  case TEST_RESERVE:
    ret = test_reserve(call.rid);
    need_reschedule = 0;
    break;
  case INIT_SERIAL:
    ret = init_serial(call.rid);
    need_reschedule = 0;
    break;
  case RECV_SERIAL:
    ret = receive(call.rid, call.buffer, call.buf_len, &flags);
    need_reschedule=1;
    break;
  case SEND_SERIAL:
    ret = send(call.rid, call.buffer, call.buf_len, 0); // no finish
    need_reschedule=0;
    break;
  case SEND_SERIAL_FINISH:
    ret = send(call.rid, call.buffer, call.buf_len,1); // finish
    need_reschedule = 1;
    break;
  case END_PERIOD:
    if (!active_rid(call.rid)){
      printk("ZSRMMV.write() ERROR got cmd(%s) with invalid/inactive rid(%d)\n",
	     STRING_ZSV_CALL(call.cmd),call.rid);
      ret = -1;
    } else {
      ret = end_of_period(call.rid);
      need_reschedule = 0;
    }
    break;
  case WAIT_RELEASE:
    if (!active_rid(call.rid)){
      printk("ZSRMMV.write() ERROR got cmd(%s) with invalid/inactive rid(%d)\n",
	     STRING_ZSV_CALL(call.cmd),call.rid);
      ret = -1;
    } else {
      //printk("ZSRMMV.write() calling wait_for_next_release(rid(%d))\n",call.rid);
      ret = wait_for_next_release(call.rid);
      //printk("ZSRMMV.write() returned from wait_for_next_release(rid(%d))\n",call.rid);
      need_reschedule = 1;
    }
    break;
  case GET_TRACE_SIZE:
    ret = trace_index;
    need_reschedule = 0;
    break;
  case WAIT_PERIOD:
    if (!active_rid(call.rid)){
      printk("ZSRMMV.write() ERROR got cmd(%s) with invalid/inactive rid(%d)\n",
	     STRING_ZSV_CALL(call.cmd),call.rid);
      ret = -1;
    } else {
      //printk("ZSRMMV.write() calling wait_for_next_period(rid(%d))\n",call.rid);
      wait_for_next_period(call.rid,0,1);
      //printk("ZSRMMV.write() returned from wait_for_next_period(rid(%d))\n",call.rid);
      ret = 0;
      need_reschedule = 1;
    }
    break;
  case NOWAIT_PERIOD:
    if (!active_rid(call.rid)){
      printk("ZSRMMV.write() ERROR got cmd(%s) with invalid/inactive rid(%d)\n",
	     STRING_ZSV_CALL(call.cmd),call.rid);
      ret = -1;
    } else {
      wait_for_next_period(call.rid,1,1);
      ret = 0;
      need_reschedule = 1;
    }
    break;
  case CREATE_RSV:
    ret = getreserve();
    if (ret >=0){
      reserve_table[ret].period.tv_sec = call.period_sec;
      reserve_table[ret].period.tv_nsec = call.period_nsec;
      reserve_table[ret].zsinstant.tv_sec = call.zsinstant_sec;
      reserve_table[ret].zsinstant.tv_nsec = call.zsinstant_nsec;

      // if hyp enforcer is negative then the hyptask does not exists
      if (call.hyp_enforcer_sec <0 || call.hyp_enforcer_nsec <0){
	reserve_table[ret].hyp_enforcer_instant.tv_sec = 0;
	reserve_table[ret].hyp_enforcer_instant.tv_nsec = 0;
	reserve_table[ret].has_hyptask = 0;
      } else {
	reserve_table[ret].hyp_enforcer_instant.tv_sec = call.hyp_enforcer_sec;
	reserve_table[ret].hyp_enforcer_instant.tv_nsec = call.hyp_enforcer_nsec;
	reserve_table[ret].has_hyptask = 1;
      }

      reserve_table[ret].period_ns = (call.period_sec * 1000000000L) +
	call.period_nsec;
      reserve_table[ret].period_ticks = ns2ticks(reserve_table[ret].period_ns);
      reserve_table[ret].execution_time.tv_sec = call.exec_sec;
      reserve_table[ret].execution_time.tv_nsec = call.exec_nsec;
      reserve_table[ret].exectime_ns = (call.exec_sec * 1000000000L) +
	call.exec_nsec;
      reserve_table[ret].exectime_ticks = ns2ticks(reserve_table[ret].exectime_ns);
      reserve_table[ret].priority = call.priority;
      reserve_table[ret].criticality = call.criticality;
      reserve_table[ret].nominal_execution_time.tv_sec = call.nominal_exec_sec;
      reserve_table[ret].nominal_execution_time.tv_nsec = call.nominal_exec_nsec;
      reserve_table[ret].nominal_exectime_ns = (call.nominal_exec_sec * 1000000000L) +
	call.nominal_exec_nsec;
      reserve_table[ret].nominal_exectime_ticks = ns2ticks(reserve_table[ret].nominal_exectime_ns);
      reserve_table[ret].zsinstant_ns = (call.zsinstant_sec * 1000000000L)+call.zsinstant_nsec;
      reserve_table[ret].hyp_enforcer_instant_ns = (call.hyp_enforcer_sec * 1000000000L) + call.hyp_enforcer_nsec;
      reserve_table[ret].hyp_enforcer_instant_ticks = ns2ticks(reserve_table[ret].hyp_enforcer_instant_ns);

      // verify if zero slack instant is the same as period set it to twice its value to ensure that it does not
      // have the possibility of triggering before the end of period (effectively disabling it).
      if (reserve_table[ret].period_ns == ((call.zsinstant_sec * 1000000000L)+call.zsinstant_nsec)){
	// simple both secs and nsecs are doubled
	reserve_table[ret].zsinstant.tv_sec *= 2;
	reserve_table[ret].zsinstant.tv_nsec *= 2;
	reserve_table[ret].has_zsenforcement = 0;
      } else {
	reserve_table[ret].has_zsenforcement = 1;
      }

      //if (admit(reserve_table, MAX_RESERVES, &reserve_table[ret],&Z)){
      if (1){
	reserve_table[ret].zsinstant_ns = Z;
	// for protection
	if (reserve_table[ret].zsinstant_ns == reserve_table[ret].period_ns){
	  reserve_table[ret].zsinstant_ns *= 2;
	}

	if (reserve_table[ret].has_zsenforcement){
	  reserve_table[ret].zsinstant = ktime_to_timespec(ns_to_ktime(reserve_table[ret].zsinstant_ns));
	}

	/* reserve_table[ret].zsinstant.tv_sec =  reserve_table[ret].zsinstant_ns / 1000000000L; */
	/* reserve_table[ret].zsinstant.tv_nsec = reserve_table[ret].zsinstant_ns % 1000000000L; */

	add_rm_queue(&reserve_table[ret]);
#ifdef __ZS_DEBUG__
	printk("zsrmv.create_rsv: rid(%d) period: sec(%ld) nsec(%ld) zs: sec(%ld), nsec(%ld) \n",
	       ret,call.period_sec, call.period_nsec,
	       call.zsinstant_sec, call.zsinstant_nsec);
#endif
      } else {
	reserve_table[ret].pid=-1; // mark unused
	ret = -1;
      }
    }
    break;
  case ATTACH_RSV:
#ifdef __ZS_DEBUG__
    printk("ZSRMMV: received attach rid(%d), pid(%d)\n",call.rid,call.pid);
#endif
    if (!active_rid(call.rid)){
      printk("ZSRMMV.write() ERROR got cmd(%s) with invalid/inactive rid(%d)\n",
	     STRING_ZSV_CALL(call.cmd),call.rid);
      ret = -1;
    } else {
      unsigned long long start_ticks, start_ns;
      if (call.pid <=0){
	printk("ZSRMMV.write() ERROR got cmd(%s) with invalid pid(%d)\n",
	       STRING_ZSV_CALL(call.cmd),call.pid);
	ret = -1;
      } else {
#ifdef __ZSV_SECURE_TASK_BOOTSTRAP__
	start_ticks = calculate_start_time(call.rid);
	start_ns = ticks2ns1(start_ticks);
	struct pid_namespace *ns = task_active_pid_ns(current);
	printk("ZSRM.attach: attach to happen in %llu ns\n",(start_ns-ticks2ns1(get_now_ticks())));
	reserve_table[call.rid].pid = call.pid;
	reserve_table[call.rid].task_namespace = task_active_pid_ns(current);
	reserve_table[call.rid].start_timer.timer_type = TIMER_START;
	reserve_table[call.rid].start_timer.expiration = ktime_to_timespec(ns_to_ktime(start_ns));
	add_timerq(&(reserve_table[call.rid].start_timer));

	// put caller task to sleep
	set_current_state(TASK_INTERRUPTIBLE);

	ret = 0;
	need_reschedule=0;
#else
	ret = attach_reserve(call.rid,call.pid);
#endif
	need_reschedule=1;
      }
    }
    break;
  case DELETE_RSV:
#ifdef __ZS_DEBUG__
    printk("ZSRMMV: received delete rid(%d)\n",call.rid);
#endif
    if (!valid_rid(call.rid)){
      printk("ZSRMMV.write() ERROR got cmd(%s) with invalid/inactive rid(%d)\n",
	     STRING_ZSV_CALL(call.cmd),call.rid);
      ret = -1;
    } else {
      ret = delete_reserve(call.rid);
      need_reschedule=1;
    }
    break;
  case GET_WCET_NS:
    if (!valid_rid(call.rid)){
      printk("ZSRMMV.write() ERROR got cmd(%s) with invalid/inactive rid(%d)\n",
	     STRING_ZSV_CALL(call.cmd),call.rid);
    } else {
      ret = get_wcet_ns(call.rid,&wcet);
      if (copy_to_user(call.pwcet, &wcet, sizeof(wcet))<0){
	printk(KERN_WARNING "ZSRMV: error copying WCET to user space\n");
      }
      need_reschedule = 0;
    }
    break;
  case GET_ACET_NS:
    ret = get_acet_ns(call.rid,&wcet);
    if (copy_to_user(call.pwcet,&wcet, sizeof(wcet))<0){
      printk(KERN_WARNING "ZSRMV: error copying ACET to user space\n");
    }
    break;
  case CAPTURE_ENFORCEMENT_SIGNAL:
#ifdef __ZS_DEBUG__
    printk("ZSRMMV: received CAPTURE ENFORCEMENT\n");
#endif
    if (!valid_rid(call.rid)){
      printk("ZSRMMV.write() ERROR got cmd(%s) with invalid/inactive rid(%d)\n",
	     STRING_ZSV_CALL(call.cmd),call.rid);
      ret = -1;
    } else {
      ret = capture_enforcement_signal(call.rid,call.pid,call.priority);
    }
    break;
  default:
    printk("ZSRMMV: ERROR UNKNOWN SYSCALL(%d)\n",call.cmd);
    ret=-1;
    break;
  }


  // enable interrupts
  spin_unlock_irqrestore(&zsrmlock,flags);

  // allow other syscalls
  // MOVED to after checking for need_reschedule to
  // avoid potential race condition
  // up(&zsrmsem);

  if (need_reschedule){
    // allow other syscalls
    up(&zsrmsem);
    schedule();
  } else {
    // allow other syscalls
    up(&zsrmsem);
  }
  return ret;
}

static long zsrm_ioctl(struct file *file,
		       unsigned int cmd,
		       unsigned long arg)
{
	return 0;
}

static struct file_operations zsrm_fops;

static void
zsrm_cleanup_module(void){

  if (dev_class){
    device_destroy(dev_class, MKDEV(dev_major, 0));
  }
  /* delete the char device. */
  cdev_del(&c_dev);

  if (dev_class)
    class_destroy(dev_class);
  /* return back the device number. */
  unregister_chrdev_region(dev_id, 1);
}

void print_overhead_stats(void)
{
  unsigned long long avg_context_switch_ns=0L;
  unsigned long long avg_enforcement_ns=0L;
  unsigned long long avg_zs_enforcement_ns=0L;
  unsigned long long avg_arrival_ns=0L;
  unsigned long long avg_blocked_arrival_ns=0L;
  unsigned long long avg_departure_ns = 0L;
  unsigned long long avg_hypercall_ns = 0L;

  if (num_hypercalls >0){
    avg_hypercall_ns = DIV(cumm_hypercall_ticks, num_hypercalls);
    avg_hypercall_ns = ticks2ns1(avg_hypercall_ns);
  }
  if (num_context_switches >0){
    avg_context_switch_ns = DIV(cumm_context_switch_ticks,num_context_switches);
    /* avg_context_switch_ns = cumm_context_switch_ticks / num_context_switches; */
    avg_context_switch_ns = ticks2ns1(avg_context_switch_ns);
  }

  if (num_enforcements>0){
    avg_enforcement_ns = DIV(cumm_enforcement_ticks,num_enforcements);
    /* avg_enforcement_ns = cumm_enforcement_ticks / num_enforcements; */
    avg_enforcement_ns = ticks2ns1(avg_enforcement_ns);
  }

  if (num_zs_enforcements>0){
    avg_zs_enforcement_ns = DIV(cumm_zs_enforcement_ticks,num_zs_enforcements);
    /* avg_zs_enforcement_ns = cumm_zs_enforcement_ticks / num_zs_enforcements; */
    avg_zs_enforcement_ns = ticks2ns1(avg_zs_enforcement_ns);
  }

  if (num_arrivals >0){
    avg_arrival_ns = DIV(cumm_arrival_ticks,num_arrivals);
    /* avg_arrival_ns = cumm_arrival_ticks / num_arrivals; */
    avg_arrival_ns = ticks2ns1(avg_arrival_ns);
  }

  if (num_blocked_arrivals>0){
    avg_blocked_arrival_ns = DIV(cumm_blocked_arrival_ticks,num_blocked_arrivals);
    /* avg_blocked_arrival_ns = cumm_blocked_arrival_ticks / num_blocked_arrivals; */
    avg_blocked_arrival_ns = ticks2ns1(avg_blocked_arrival_ns);
  }

  if (num_departures >0){
    avg_departure_ns = DIV(cumm_departure_ticks,num_departures);
    /* avg_departure_ns = cumm_departure_ticks / num_departures; */
    avg_departure_ns = ticks2ns1(avg_departure_ns);
  }

  printk("zsrmv *** OVERHEAD STATS *** \n");
  printk("avg hypercall ns: %llu \t wc hypercall ns: %llu \t num hypercalls: %llu \n",
	 avg_hypercall_ns, ticks2ns1(wc_hypercall_ticks), num_hypercalls);
  printk("avg context switch ns: %llu \t wc context switch ns: %llu \t num context switch %llu \n",
	 avg_context_switch_ns, ticks2ns1(wc_context_switch_ticks), num_context_switches);
  printk("avg enforcement ns: %llu \t wc enforcement ns: %llu \t num enforcements: %llu \n",
	 avg_enforcement_ns, ticks2ns1(wc_enforcement_ticks), num_enforcements);
  printk("avg zs enforcement ns: %llu num zs enforcements: %llu \n",
	 avg_zs_enforcement_ns, num_zs_enforcements);
  printk("avg arrival ns: %llu \t wc arrival ns: %llu \t num arrivals: %llu \n",
	 avg_arrival_ns, ticks2ns1(wc_arrival_ticks), num_arrivals);
  printk("avg blocked arrival ns: %llu num blocked arrivals: %llu\n",
	 avg_blocked_arrival_ns, num_blocked_arrivals);
  printk("avg departure ns: %llu \t wc departure ns: %llu \t num departures: %llu\n",
	 avg_departure_ns, ticks2ns1(wc_departure_ticks), num_departures);
  printk("zsrmv *** END OVERHEAD STATS *** \n");
}

static struct proc_dir_entry *proc_file = NULL;
static struct file_operations proc_fops;

/* dummy function. */
static int proc_open(struct inode *inode, struct file *filp)
{
  return 0;
}

/* dummy function. */
static int proc_release(struct inode *inode, struct file *filp)
{
  return 0;
}

static ssize_t proc_read(struct file *filp,	/* see include/linux/fs.h   */
			   char *buffer,	/* buffer to fill with data */
			   size_t length,	/* length of the buffer     */
			   loff_t * offset)
{
    int len;
    static int eof=0;

    if (!eof){
      len = snprintf(buffer,length,"Receiver:%s \nSender:%s \nHWR RCV:%s \nHWR SND: %s \nRecv buffer: %s readIdx(%d) writeIdx(%d)\nTrans buffer: %s readIdx(%d) writeIdx(%d)\n#errors: %d\nLast Non-Zero Receive Count: %d\nNum Zero-Receives count:%lld\nLast receive count:%d\nLargest read count: %d\nMax sleep:%lld\n",
		     ((serial_debug_flags & SERIAL_FLAG_RCV_READ_BLOCKED)? "BLOCKED" : "RUNNING"),
		     ((serial_debug_flags & SERIAL_FLAG_SND_READ_BLOCKED)? "BLOCKED" : "RUNNING"),
		     ((serial_is_reception_stopped())? "STOPPED" : "FREE"),
		     ((serial_debug_flags & SERIAL_FLAG_HWR_SND_BLOCKED)? "STOPPED" : "FREE"),
		     ((serial_receiving_writing_index == serial_receiving_reading_index)? "EMPTY" : "DATA"),
		     serial_receiving_reading_index,
		     serial_receiving_writing_index,
		     ((serial_sending_writing_index == serial_sending_reading_index)? "EMPTY" : "DATA"),
		     serial_sending_reading_index,
		     serial_sending_writing_index,
		     serial_receiving_error_count,
		     serial_debug_last_non_zero_receive_count,
		     serial_debug_num_zero_receive_counts,
		     serial_debug_last_receive_count,
		     serial_debug_largest_read_count,
		     ticks2ns1(serial_debug_max_sleep_ticks)
		    );
    } else {
      // send eof
      len = 0 ;
    }

    eof = 1-eof;

    return len;
}

static ssize_t proc_write (struct file *file, const char *buf, size_t count, loff_t *offset)
{
}

static long proc_ioctl(struct file *file,
		       unsigned int cmd,
		       unsigned long arg)
{
	return 0;
}


static int __init zsrm_init(void)
{
  int ret;
  int err = 0;
  dev_t devno;
  struct device *device = NULL;
  struct sched_param p;

  unsigned long long start_ns;
  unsigned long long end_ns;

  ktime_t ktime;

  /* u64 start_tick; */
  /* u64 end_tick; */
  int cnt;

  printk("ZSRMV.init(): cts_gpio_pin set to %d\n",cts_gpio_pin);

  proc_fops.owner = THIS_MODULE;
  proc_fops.open = proc_open;
  proc_fops.release = proc_release;
  proc_fops.read = proc_read;
  proc_fops.write = proc_write;
  proc_fops.unlocked_ioctl = proc_ioctl;

  proc_file = proc_create("zsrmv", 0666, NULL, &proc_fops);

  if (proc_file == NULL){
    printk("ZSRM.init(): could not create proc file\n");
    remove_proc_entry("zsrmv",NULL);
  }

  // initialize scheduling top
  top = -1;

  // initialize semaphore
  sema_init(&zsrmsem,1); // binary - initially unlocked

  // serial buffer semaphore
  // binary -- initially unlocked
  sema_init(&serial_buffer_sem,1);

  sema_init(&serial_sending_buffer_sem,1);

  init();
  printk(KERN_INFO "ZSRMMV: HELLO!\n");

  /* get the device number of a char device. */
  ret = alloc_chrdev_region(&dev_id, 0, 1, DEVICE_NAME);
  if (ret < 0) {
    printk(KERN_WARNING "ZSRMMV: failed to allocate device.\n");
    return ret;
  }

  dev_major = MAJOR(dev_id);

  dev_class = class_create(THIS_MODULE,DEVICE_NAME);
  if (IS_ERR(dev_class)){
    printk(KERN_WARNING "ZSRMMV: failed to create device class.\n");
    err = PTR_ERR(dev_class);
    dev_class = NULL;
    zsrm_cleanup_module();
    return err;
  }

  devno = MKDEV(dev_major, 0);

  // initialize the fops
  zsrm_fops.owner = THIS_MODULE;
  zsrm_fops.open = zsrm_open;
  zsrm_fops.release = zsrm_release;
  zsrm_fops.read = zsrm_read;
  zsrm_fops.write = zsrm_write;

  //#if LINUX_KERNEL_MINOR_VERSION < 37
  //zsrm_fops.ioctl = zsrm_ioctl;
  //#else
  zsrm_fops.unlocked_ioctl = zsrm_ioctl;
  //#endif

  /* initialize the char device. */
  cdev_init(&c_dev, &zsrm_fops);

  /* register the char device. */
  ret = cdev_add(&c_dev, dev_id, 1);
  if (ret < 0) {
    printk(KERN_WARNING "ZSRMMV: failed to register device.\n");
    return ret;
  }

  device = device_create(dev_class, NULL, devno, NULL, DEVICE_NAME "%d", 0);

  if (IS_ERR(device)){
    err = PTR_ERR(device);
    printk(KERN_WARNING "ZSRMMV: Error %d while trying to create dev %s%d", err, DEVICE_NAME,0);
    cdev_del(&c_dev);
    return err;
  }

  // Start scheduler task
  sched_task = kthread_create((void *)scheduler_task, NULL, "ZSRMMV scheduler thread");
  p.sched_priority = DAEMON_PRIORITY;

  if (sched_setscheduler(sched_task, SCHED_FIFO, &p)<0){
    printk("ZSRMMV.init() error setting sched_task kernel thead priority\n");
  }

  kthread_bind(sched_task, 0);

  // Start activator task
  active_task = kthread_create((void *)activator_task, NULL, "Activator thread");
  p.sched_priority = DAEMON_PRIORITY;

  if (sched_setscheduler(active_task, SCHED_FIFO, &p)<0){
    printk("ZSRMMV.init() error setting active_task kernel thead priority\n");
  }

  kthread_bind(active_task, 0);


  // For serial hardware control flow
#ifdef __SERIAL_HARDWARE_CONTROL_FLOW__
  if (gpio_request(GPIO_RTS, "RTS") <0){
    printk("ZSRM.init(): error requesting gpio pin 17\n");
  }
  if (gpio_request(cts_gpio_pin, "CTS")<0){
    printk("ZSRM.init(): error requesting gpio pin 18\n");
  }

  if (gpio_direction_input(cts_gpio_pin)<0){
    printk("ZSRM.init(): error setting input direction for CTS\n");
  }

  if (gpio_direction_output(GPIO_RTS,0)<0){
    printk("ZSRM.init(): error setting output direction and setting RTS to zero\n");
  }
#endif

#ifdef __START_SERIAL_RECEIVER_TASK__
  // Start serial receiver task
  serial_recv_task = kthread_create((void *)serial_receiver_task, NULL, "Serial receiver thread");

  printk("ZSRMV: created serial receiver task ptr=%x\n",serial_recv_task);

  p.sched_priority = RECEIVER_PRIORITY;

  if (sched_setscheduler(serial_recv_task, SCHED_FIFO, &p)<0){
    printk("ZSRMMV.init() error setting serial_receiver_task kernel thead priority\n");
  }

  kthread_bind(serial_recv_task, 0);

  if (serial_recv_task){
    wake_up_process(serial_recv_task);
    serial_timer_prev_timestamp_ticks = get_now_ticks();

    /* hrtimer_init(&serial_receiver_kernel_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL); */
    /* serial_receiver_kernel_timer.function = serial_kernel_timer_handler; */
    /* serial_timer_period_ns = 300000; */
    /* ktime = ns_to_ktime(serial_timer_period_ns); */
    /* hrtimer_start(&serial_receiver_kernel_timer, ktime , HRTIMER_MODE_REL); */
  }
#endif


    // Start serial sender task
  serial_sender_task = kthread_create((void *)serial_sending_task, NULL, "Serial sender thread");

  printk("ZSRMV: created serial sender task ptr=%x\n",serial_sender_task);

  p.sched_priority = DAEMON_PRIORITY;

  if (sched_setscheduler(serial_sender_task, SCHED_FIFO, &p)<0){
    printk("ZSRMMV.init() error setting serial_sender_task kernel thead priority\n");
  }

  kthread_bind(serial_sender_task, 0);

  if (serial_sender_task){
    wake_up_process(serial_sender_task);
  }

  init_cputsc();

  //hypmtscheduler_inittsc();

  setup_ticksclock();

  /* start_tick = sysreg_read_cntpct(); */
  /* hypmtscheduler_getrawtick64(&start_tick); */
  start_tick =  get_now_ticks(); //rdtsc64();
  start_ns = get_now_ns();

  for (cnt =0 ; cnt <100000;cnt++)
    ret=cnt+1;
  end_ns = get_now_ns();
  end_tick =  get_now_ticks(); //rdtsc64();
  /* end_tick = sysreg_read_cntpct(); */
  /* hypmtscheduler_getrawtick64(&end_tick); */

  printk("ZSRMV: cycle counter test (for-loop 1000) start(%llu) end(%llu) count(%llu) count_ns(%llu) elapsed_ns(%llu) \n",start_tick, end_tick, (end_tick-start_tick), ticks2ns(end_tick-start_tick), (end_ns-start_ns));

  printk(KERN_WARNING "ZSRMMV: ready!\n");

  return 0;
}


static void __exit zsrm_exit(void)
{
  // empty rescheduling stack
  top = -1;
  kthread_stop(sched_task);

  activate_top = -1;
  kthread_stop(active_task);

#ifdef  __START_SERIAL_RECEIVER_TASK__
  wake_up_process(serial_recv_task);
  kthread_stop(serial_recv_task);

  //hrtimer_cancel(&serial_receiver_kernel_timer);
#endif


  sending_task_active=0;
  wake_up_interruptible(&serial_write_wait_queue);
  wake_up_process(serial_sender_task);
  kthread_stop(serial_sender_task);


  /* if (serial_recv_task_running){ */
  /*   serial_recv_task_running = 0; */
  /*   if(!hypmtscheduler_deletehyptask(hyp_serial_recv_task_handle)){ */
  /*     printk("ZSRM.exit(): error deleting serial receiver hyper task\n"); */
  /*   } */
  /* } */

  print_overhead_stats();

  end_tick = sysreg_read_cntpct(); //rdtsc64();
  printk("ZSRMV: cycle counter test start(%llu) end(%llu) count=%llu\n",start_tick, end_tick, (end_tick-start_tick));

  printk(KERN_INFO "ZSRMMV: GOODBYE!\n");

  zsrm_cleanup_module();

#ifdef __SERIAL_HARDWARE_CONTROL_FLOW__
  gpio_free(cts_gpio_pin);
  gpio_free(GPIO_RTS);
#endif

  if (proc_file != NULL){
    proc_remove(proc_file);
  }
}

module_init(zsrm_init);
module_exit(zsrm_exit);
