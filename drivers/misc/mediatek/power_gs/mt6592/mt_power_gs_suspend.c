#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/seq_file.h>

#include <mach/mt_typedefs.h>
#include <mach/mt_power_gs.h>

extern unsigned int *AP_CG_gs_suspend;
extern unsigned int AP_CG_gs_suspend_len;

extern unsigned int *AP_DCM_gs_suspend;
extern unsigned int AP_DCM_gs_suspend_len;

extern unsigned int *PMIC23_LDO_BUCK_gs_suspend;
extern unsigned int PMIC23_LDO_BUCK_gs_suspend_len;

extern unsigned int *CHG33_CHG_BUCK_gs_suspend;
extern unsigned int CHG33_CHG_BUCK_gs_suspend_len;

void mt_power_gs_dump_suspend(void)
{
    mt_power_gs_compare("Suspend",                                \
                        AP_CG_gs_suspend, AP_CG_gs_suspend_len,   \
                        AP_DCM_gs_suspend, AP_DCM_gs_suspend_len, \
                        PMIC23_LDO_BUCK_gs_suspend, PMIC23_LDO_BUCK_gs_suspend_len, \
                        CHG33_CHG_BUCK_gs_suspend, CHG33_CHG_BUCK_gs_suspend_len);
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 10, 0)
static int dump_suspend_read(char *buf, char **start, off_t off, int count, int *eof, void *data)
{
    int len = 0;
    char *p = buf;

    p += sprintf(p, "mt_power_gs : suspend\n");

    mt_power_gs_dump_suspend();

    len = p - buf;
    return len;
}
#else
static int dump_suspend_read(struct seq_file *m, void *v)
{
    seq_printf(m, "mt_power_gs : suspend\n");
    mt_power_gs_dump_suspend();
    return 0;
}

static int proc_mt_power_gs_dump_suspend_open(struct inode *inode, struct file *file)
{
    return single_open(file, dump_suspend_read, NULL);
}
static const struct file_operations mt_power_gs_dump_suspend_proc_fops = {
    .owner = THIS_MODULE,
    .open  = proc_mt_power_gs_dump_suspend_open, 
    .read  = seq_read,
};
#endif

static void __exit mt_power_gs_suspend_exit(void)
{
    //return 0;
}

static int __init mt_power_gs_suspend_init(void)
{
    struct proc_dir_entry *mt_entry = NULL;

    if (!mt_power_gs_dir)
    {
		pr_err("mkdir /proc/mt_power_gs failed\n");
    }
    else
    {
        #if LINUX_VERSION_CODE < KERNEL_VERSION(3, 10, 0)
        mt_entry = create_proc_entry("dump_suspend", S_IRUGO | S_IWUSR | S_IWGRP, mt_power_gs_dir);
        if (mt_entry)
        {
            mt_entry->read_proc = dump_suspend_read;
        }
        #else
        if (proc_create("dump_suspend", S_IRUGO | S_IWUSR | S_IWGRP, mt_power_gs_dir, &mt_power_gs_dump_suspend_proc_fops) == NULL) {
			pr_err("create_proc_entry dump_suspend failed\n");
        }
        #endif
    }

    return 0;
}

module_init(mt_power_gs_suspend_init);
module_exit(mt_power_gs_suspend_exit);

MODULE_DESCRIPTION("MT Power Golden Setting - Suspend");