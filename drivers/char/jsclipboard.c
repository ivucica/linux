/* 
   JS clipboard support for JS/Linux
   (c) 2011 Fabrice Bellard
*/
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/miscdevice.h>
#include <linux/ioport.h>
#include <linux/fcntl.h>
#include <linux/mc146818rtc.h>
#include <linux/init.h>
#include <linux/poll.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/spinlock.h>
#include <linux/sysctl.h>
#include <linux/wait.h>
#include <linux/bcd.h>
#include <linux/delay.h>

#include <asm/current.h>
#include <asm/uaccess.h>
#include <asm/system.h>

#define JSCLIPBOARD_MINOR 231

#define JSCLIPBOARD_PORT 0x3c0

static int io_port = JSCLIPBOARD_PORT;
static int minor = JSCLIPBOARD_MINOR;
static struct semaphore open_sem;
static int need_cache_sync;

module_param(io_port, int, 0);
MODULE_PARM_DESC(io_port, "IO port");

module_param(minor, int, 0);
MODULE_PARM_DESC(minor, "minor number");

static ssize_t jsclipboard_read(struct file *file, char __user *buf,
                                size_t count1, loff_t *ppos)
{
        uint32_t pos, total_length, v;
        uint8_t b;
        size_t count, l;
        
        /* set read position */
        pos = *ppos;
        outl(pos, io_port + 4);
        total_length = inl(io_port + 0);
        
        if (!access_ok(VERIFY_WRITE, buf, count1))
                return -EFAULT;
        
        if (pos < total_length) 
                l = total_length - pos;
        else
                l = 0;
        if (count1 > l)
                count1 = l;
        count = count1;
        while (count >= 4) {
                v = inl(io_port + 8);
                if (__put_user(v, (uint32_t *)buf))
                        return -EFAULT;
                buf += 4;
                count -= 4;
        }
        
        while (count != 0) {
                b = inb(io_port + 8);
                if (__put_user(b, buf))
                        return -EFAULT;
                buf++;
                count--;
        }

        *ppos = pos + count1;

        return count1;
}

static ssize_t jsclipboard_write(struct file *file, const char *buf, 
                                 size_t count1, loff_t *ppos)
{
        size_t count;
        uint8_t b;
        uint32_t v;

        if (!access_ok(VERIFY_READ, buf, count1))
                return -EFAULT;
        if (*ppos == 0) {
                /* flush clipboard */
                outl(0, io_port);
        }

        need_cache_sync = 1;

        count = count1;
        while (count >= 4) {
                if (__get_user(v, (uint32_t *)buf))
                        return -EFAULT;
                outl(v, io_port + 8);
                buf += 4;
                count -= 4;
        }

        while (count != 0) {
                if (__get_user(b, buf))
                        return -EFAULT;
                outb(b, io_port + 8);
                buf++;
                count--;
        }
        *ppos += count1;
        return count1;
}

static int jsclipboard_open(struct inode *inode, struct file *file)
{
	if (down_trylock(&open_sem))
		return -EBUSY;
        need_cache_sync = 0;
	return 0;
}

static int jsclipboard_release(struct inode *inode, struct file *file)
{
        if (need_cache_sync) {
                outl(0, io_port + 12);
        }
	up(&open_sem);
	return 0;
}

static const struct file_operations jsclipboard_fops = {
	.owner		= THIS_MODULE,
	.read		= jsclipboard_read,
	.write		= jsclipboard_write,
	.open		= jsclipboard_open,
        .release        = jsclipboard_release,
};

static struct miscdevice jsclipboard_dev = {
	.minor		= JSCLIPBOARD_MINOR,
	.name		= "jsclipboard",
	.fops		= &jsclipboard_fops,
};

static int __init jsclipboard_init(void)
{
        if (!request_region(io_port, 16, "jsclipboard")) 
                return -ENODEV;
	sema_init(&open_sem, 1);
	if (misc_register(&jsclipboard_dev)) {
                return -ENODEV;
	}
	printk(KERN_INFO "JS clipboard: I/O at 0x%04x\n", io_port);
	return 0;
}

static void __exit jsclipboard_exit (void)
{
	misc_deregister(&jsclipboard_dev);
        release_region(io_port, 16);
}

module_init(jsclipboard_init);
module_exit(jsclipboard_exit);

MODULE_AUTHOR("Fabrice Bellard");
MODULE_LICENSE("GPL");
