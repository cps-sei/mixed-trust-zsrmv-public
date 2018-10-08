#include <linux/module.h>

#define UPPER_ADDRESS		0xEE000000
#define LOWER_ADDRESS		0xC0000000

#define PMCNTNSET_C_BIT		0x80000000
#define PMCR_D_BIT		0x00000008
#define PMCR_C_BIT		0x00000004
#define PMCR_P_BIT		0x00000002
#define PMCR_E_BIT		0x00000001

inline void start_tsc(void){
	unsigned long tmp;

	tmp = PMCNTNSET_C_BIT;
	asm volatile ("mcr p15, 0, %0, c9, c12, 1" : : "r" (tmp));


	asm volatile ("mrc p15, 0, %0, c9, c12, 0" : "=r" (tmp));
	tmp |= PMCR_C_BIT | PMCR_E_BIT;
	asm volatile ("mcr p15, 0, %0, c9, c12, 0" : : "r" (tmp));
}

inline unsigned long rd_tsc(void){
	unsigned long result;
	asm volatile ("mrc p15, 0, %0, c9, c13, 0" : "=r" (result));
	return result;
}

u64 rdtsc64(void){
	u32 tsc_lo, tsc_hi;
	u64 l_tickcount;

	asm volatile
		(	" isb\r\n"
			" mrrc p15, 0, r0, r1, c9 \r\n"
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


static int __init rdtsc_init(void)
{
  u64 tmp;
  u64 tmp1;
  printk("rdtsc STARTED\n");
  start_tsc();
  tmp = rdtsc64();
  printk("tsc 1: %llu\n",tmp);
  tmp = rdtsc64();
  printk("tsc 2: %llu\n",tmp);
  tmp = rdtsc64();
  printk("tsc 3: %llu\n",tmp);
  tmp = rdtsc64();
  printk("tsc 4: %llu\n",tmp);
  tmp = rdtsc64();
  printk("tsc 5: %llu\n",tmp);
  tmp = rdtsc64();
  printk("tsc 6: %llu\n",tmp);
  tmp = rdtsc64();
  printk("tsc 7: %llu\n",tmp);
  tmp = rdtsc64();
  tmp1 = rdtsc64();
  printk("tsc 8: %llu\n",tmp);
  printk("tsc 8+': %llu\n",tmp1);
  return 0;
}

static void __exit rdtsc_exit(void)
{
  printk("rdtsc: EXIT\n");
}

module_init(rdtsc_init);
module_exit(rdtsc_exit);
