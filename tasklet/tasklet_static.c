#include<linux/module.h>
#include<linux/init.h>
#include<linux/fs.h>
#include<linux/cdev.h>
#include<linux/device.h>
#include<linux/kdev_t.h>
#include<linux/uaccess.h>
#include<linux/interrupt.h>

#define DEV_MEM_SIZE 512  // limit of this driver

#define IRQ_NO 1

#undef pr_fmt 
#define pr_fmt(fmt) "%s:" fmt,__func__

/*
 * pseudo device's memory
 * */
static char device_buffer[DEV_MEM_SIZE];

/*
 * This holds the device number
 * */
static dev_t device_number;

/* 
 * Cdev variable 
 * */
static struct cdev pcd_cdev;

void tasklet_fn(struct tasklet_struct *);

struct tasklet_struct tasklet;


/*init Tasklet using static method*/
DECLARE_TASKLET(tasklet,tasklet_fn);

/*tasklet function*/
void tasklet_fn(struct tasklet_struct *value)
{
	pr_info("Executing tasklet function\n");
}



/*interrupt handler for irq 11*/
static irqreturn_t irq_handler(int irq, void *dev_id)
{
	pr_info("INterrupt occurred\n");

	/*scheduling*/
	tasklet_schedule(&tasklet);

	return IRQ_HANDLED;
}




loff_t pcd_lseek(struct file *filp, loff_t offset, int whence)
{
	loff_t temp;
	pr_info("lseek requested\n");
	pr_info("current value of the file position = %lld\n",filp->f_pos);

	switch(whence)
	{
		case SEEK_SET:
			if((offset > DEV_MEM_SIZE) || (offset < 0))
				return -EINVAL;
			filp->f_pos = offset;
			break;
		case SEEK_CUR:
			temp = filp->f_pos + offset;
			if((temp > DEV_MEM_SIZE) || (temp < 0))
				return -EINVAL;
			filp->f_pos = temp;
			break;
		case SEEK_END:
			temp = DEV_MEM_SIZE + offset;
			if((temp > DEV_MEM_SIZE) || (temp < 0))
				return -EINVAL;

			filp->f_pos = temp;
			break;
		default:
			return -EINVAL;
	}

	pr_info("New value  of the file position = %lld\n",filp->f_pos);
        return filp->f_pos;
}
ssize_t pcd_read(struct file *filp, char __user *buff, size_t count, loff_t *f_pos)
{
	pr_info("read requested for %zu bytes \n",count);
	pr_info("current file position = %lld\n",*f_pos);
	/*
	 * Adjust the 'count'
	 * */
	if((*f_pos +count)>DEV_MEM_SIZE)
		count = DEV_MEM_SIZE - *f_pos;

	/*
	 * copy to user
	 * */
	if(copy_to_user(buff,&device_buffer[*f_pos],count)){
		return -EFAULT;
	}
	/*
	 * update the current file position
	 * */

	*f_pos+=count;

	pr_info("Number of bytes successfully read = %zu\n",count);
	pr_info("Updated file position = %lld\n",*f_pos);

	/*
	 * return number of bytes which have been successfully read
	 * */
        return count;
}
ssize_t pcd_write(struct file *filp, const char __user *buff, size_t count, loff_t *f_pos)
{
	pr_info("write requested for %zu bytes \n",count);
	pr_info("current file position = %lld\n",*f_pos);

	/*
	 * Adjust the 'count'
	 * */
	if((*f_pos + count)>DEV_MEM_SIZE)
		count = DEV_MEM_SIZE - *f_pos;
	
	if(!count){
		pr_err("No space left on the device\n");
		return -ENOMEM;
	}

	/*
	 * copt from user
	 * */
	if(copy_from_user(&device_buffer[*f_pos],buff,count)){
		return -EFAULT;
	}

	/*
	 * update the current file position
	 * */
	*f_pos += count;
	 
	pr_info("Number of bytes successfully written = %zu\n",count);
	pr_info("Updated file position = %lld\n",*f_pos);

	/*
	 * Return number of bytes which have been successfully written
	 * */
	return count;
}
int pcd_open(struct inode *inode, struct file *filp)
{
	pr_info("open was successful\n");

        return 0;
}
int pcd_release(struct inode *inode, struct file *filp)
{
	pr_info("release was successful\n");

        return 0;
}





/* file operations of the driver */

struct file_operations pcd_fops = 
{
	.open = pcd_open,
	.write = pcd_write,
	.read = pcd_read,
	.release = pcd_release,
	.llseek = pcd_lseek,
	.owner = THIS_MODULE
};

struct class *class_pcd;

struct device *device_pcd;


static int __init pcd_driver_init(void){

	int ret;

	/*
	 * 1. Dynamically allocate a device number
	 * */
	ret = alloc_chrdev_region(&device_number,0,7,"pcd_device");
	if(ret < 0){
		pr_err("Alloc chrdev failed\n");
		goto out;
	}

	pr_info("Device number <major>:<minor> = %d:%d\n",MAJOR(device_number),MINOR(device_number));

	/*
	 * 2. Initialize the cdev structure with fops
	 * */
	cdev_init(&pcd_cdev,&pcd_fops);

	/*
	 * 3. Register cdev device (cdev strucure) with VFS
	 * */
	pcd_cdev.owner=THIS_MODULE;
	ret = cdev_add(&pcd_cdev,device_number,1);
	if(ret < 0){
		pr_err("cdev_add failed\n");
		goto unreg_chrdev;
	}

	/*
	 * 4. Creating device file 
	 * */
	/*
	 * Create device class under /sys/class
	 * */
	class_pcd = class_create(THIS_MODULE,"pcd_class");
	/*
	 * class create returns poniter. So while failing-
	 * it returns pointer to negative value. So the macro-
	 * IS_ERR is used
	 * */
	if(IS_ERR(class_pcd))
	{
		pr_err("Class creation failed\n");
		/*
		 * PTR_ERR() converts pointer to error code(int)
		 * ERR_PTR() converts error code (int) to pointer
		 * */
		ret = PTR_ERR(class_pcd);
		goto cdev_del;
	}


	/*
	 * Populate the sysfs with device information
	 * */
	device_pcd = device_create(class_pcd,NULL,device_number,NULL,"pcd");
	if(IS_ERR(device_create))
	{
		pr_err("Device create failed\n");
		ret = PTR_ERR(device_pcd);
		goto class_del;
	}


	ret = request_irq(IRQ_NO,irq_handler,IRQF_SHARED,"pcd",(void*)(irq_handler));
	if(ret < 0){
		pr_info("irq request faild\n");
		goto device_del;
	}

	pr_info("Module init was successful\n");


	return 0;

device_del:
	device_destroy(class_pcd,device_number);

class_del:
	class_destroy(class_pcd);
cdev_del:
	cdev_del(&pcd_cdev);
unreg_chrdev:
	unregister_chrdev_region(device_number,1);
out:
	pr_info("Module insertion failed\n");
	return ret;

}



static void __exit pcd_driver_cleanup(void){

	tasklet_kill(&tasklet);
	free_irq(IRQ_NO,(void *)(irq_handler));
	device_destroy(class_pcd,device_number);
	class_destroy(class_pcd);
	cdev_del(&pcd_cdev);
	unregister_chrdev_region(device_number,1);

	pr_info("Module unloaded\n");

}

module_init(pcd_driver_init);
module_exit(pcd_driver_cleanup);


MODULE_LICENSE("GPL");
MODULE_AUTHOR("Yogesh");
MODULE_DESCRIPTION("pcd");

