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


static int __init rdtsc_init(void)
{
  unsigned long tmp;
  unsigned long tmp1;
  printk("rdtsc STARTED\n");
  start_tsc();
  tmp = rd_tsc();
  printk("tsc 1: %lu\n",tmp);
  tmp = rd_tsc();
  printk("tsc 2: %lu\n",tmp);
  tmp = rd_tsc();
  printk("tsc 3: %lu\n",tmp);
  tmp = rd_tsc();
  printk("tsc 4: %lu\n",tmp);
  tmp = rd_tsc();
  printk("tsc 5: %lu\n",tmp);
  tmp = rd_tsc();
  printk("tsc 6: %lu\n",tmp);
  tmp = rd_tsc();
  printk("tsc 7: %lu\n",tmp);
  tmp = rd_tsc();
  tmp1 = rd_tsc();
  printk("tsc 8: %lu\n",tmp);
  printk("tsc 8': %lu\n",tmp1);
  return 0;
}

static void __exit rdtsc_exit(void)
{
  printk("rdtsc: EXIT\n");
}

module_init(rdtsc_init);
module_exit(rdtsc_exit);
