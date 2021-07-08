// SPDX-License-Identifier: GPL-2.0-only
/*
 * Remote processor messaging camera driver.
 *
 * Based on Roger Quadros' original patch [1] to expose interfaces to
 * user space, allowing applications to communicate with the PRU cores.
 *
 * [1] [PATCH v2 14/14] rpmsg: pru: add a PRU RPMsg driver
 * https://lore.kernel.org/linux-omap/1549290167-876-15-git-send-email-rogerq@ti.com/
 *
 * Copyright (C) 2021 Cristian Ciocaltea <cristian.ciocaltea@gmail.com>
 */

#include <linux/cdev.h>
#include <linux/kernel.h>
#include <linux/kfifo.h>
#include <linux/module.h>
#include <linux/rpmsg.h>

#define PRU_MAX_DEVICES			8

/* Max size of the buffer (see MAX_RPMSG_BUF_SIZE in virtio_rpmsg_bus.c) */
#define RPMSG_BUF_SIZE			512
/* Size of the buffer header (see struct rpmsg_hdr in virtio_rpmsg_bus.c) */
#define RPMSG_HEADER_SIZE		16

#define MAX_FIFO_MSG			32
#define FIFO_MSG_SIZE			RPMSG_BUF_SIZE

/**
 * struct rpmsgcam_priv - Structure that contains the per-device data
 * @rpdev: rpmsg channel device that is associated with this rpmsg_pru device
 * @dev: device
 * @cdev: character device
 * @locked: boolean used to determine whether or not the device file is in use
 * @devt: dev_t structure for the rpmsg_pru device
 * @msg_fifo: kernel fifo used to buffer the messages between userspace and PRU
 * @msg_len: array storing the lengths of each message in the kernel fifo
 * @msg_idx_rd: kernel fifo read index
 * @msg_idx_wr: kernel fifo write index
 * @wait_list: wait queue used to implement the poll operation of the character
 *             device
 *
 * Each rpmsg_pru device provides an interface, using an rpmsg channel (rpdev),
 * between a user space character device (cdev) and a PRU core. A kernel fifo
 * (msg_fifo) is used to buffer the messages in the kernel that are
 * being passed between the character device and the PRU.
 */
struct rpmsgcam_priv {
	struct rpmsg_device *rpdev;
	struct device *dev;
	struct cdev cdev;
	bool locked;
	dev_t devt;
	struct kfifo msg_fifo;
	u32 msg_len[MAX_FIFO_MSG];
	int msg_idx_rd;
	int msg_idx_wr;
	wait_queue_head_t wait_list;
};

static struct class *rpmsgcam_class;
static dev_t rpmsgcam_privt;
static DEFINE_MUTEX(rpmsgcam_lock);
static DEFINE_IDR(rpmsgcam_minors);

static int rpmsgcam_open(struct inode *inode, struct file *filp)
{
	struct rpmsgcam_priv *priv;
	int ret = -EACCES;

	priv = container_of(inode->i_cdev, struct rpmsgcam_priv, cdev);

	mutex_lock(&rpmsgcam_lock);
	if (!priv->locked) {
		priv->locked = true;
		filp->private_data = priv;
		ret = 0;
	}
	mutex_unlock(&rpmsgcam_lock);

	if (ret)
		dev_err(priv->dev, "Device already open\n");

	return ret;
}

static int rpmsgcam_release(struct inode *inode, struct file *filp)
{
	struct rpmsgcam_priv *priv;

	priv = container_of(inode->i_cdev, struct rpmsgcam_priv, cdev);
	mutex_lock(&rpmsgcam_lock);
	priv->locked = false;
	mutex_unlock(&rpmsgcam_lock);

	return 0;
}

/*
 * Makes data from PRU available to user space.
 */
static ssize_t rpmsgcam_read(struct file *filp, char __user *buf,
			     size_t count, loff_t *f_pos)
{
	int ret;
	u32 length;
	struct rpmsgcam_priv *priv;

	priv = filp->private_data;

	if (kfifo_is_empty(&priv->msg_fifo) && (filp->f_flags & O_NONBLOCK))
		return -EAGAIN;

	ret = wait_event_interruptible(priv->wait_list,
				       !kfifo_is_empty(&priv->msg_fifo));
	if (ret)
		return -EINTR;

	ret = kfifo_to_user(&priv->msg_fifo, buf,
			    priv->msg_len[priv->msg_idx_rd], &length);
	priv->msg_idx_rd = (priv->msg_idx_rd + 1) % MAX_FIFO_MSG;

	return ret ? ret : length;
}

/*
 * Sends data from user space to PRU.
 */
static ssize_t rpmsgcam_write(struct file *filp, const char __user *buf,
			      size_t count, loff_t *f_pos)
{
	int ret;
	struct rpmsgcam_priv *priv;
	static char rpmsgcam_buf[RPMSG_BUF_SIZE];

	priv = filp->private_data;

	if (count > RPMSG_BUF_SIZE - RPMSG_HEADER_SIZE) {
		dev_err(priv->dev, "Data too large for RPMsg Buffer\n");
		return -EINVAL;
	}

	if (copy_from_user(rpmsgcam_buf, buf, count)) {
		dev_err(priv->dev, "Error copying buffer from user space");
		return -EFAULT;
	}

	ret = rpmsg_send(priv->rpdev->ept, (void *)rpmsgcam_buf, count);
	if (ret)
		dev_err(priv->dev, "rpmsg_send failed: %d\n", ret);

	return ret ? ret : count;
}

static unsigned int rpmsgcam_poll(struct file *filp,
				  struct poll_table_struct *wait)
{
	int mask;
	struct rpmsgcam_priv *priv;

	priv = filp->private_data;

	poll_wait(filp, &priv->wait_list, wait);

	mask = POLLOUT | POLLWRNORM;

	if (!kfifo_is_empty(&priv->msg_fifo))
		mask |= POLLIN | POLLRDNORM;

	return mask;
}

static const struct file_operations rpmsgcam_fops = {
	.owner		= THIS_MODULE,
	.open		= rpmsgcam_open,
	.release	= rpmsgcam_release,
	.read		= rpmsgcam_read,
	.write		= rpmsgcam_write,
	.poll		= rpmsgcam_poll,
};

static int rpmsgcam_cb(struct rpmsg_device *rpdev,
		       void *data, int len, void *cbpriv, u32 src)
{
	struct rpmsgcam_priv *priv = dev_get_drvdata(&rpdev->dev);
	u32 length;

	dev_dbg(&rpdev->dev, "incoming msg (len: %d, src: 0x%x)\n", len, src);
	print_hex_dump_debug("rpmsgcam data: ", DUMP_PREFIX_NONE, 16, 1, data, len, true);

	if (kfifo_avail(&priv->msg_fifo) < len) {
		dev_err(&rpdev->dev, "Not enough space on the FIFO\n");
		return -ENOSPC;
	}

	if ((priv->msg_idx_wr + 1) % MAX_FIFO_MSG == priv->msg_idx_rd) {
		dev_err(&rpdev->dev, "Message length table is full\n");
		return -ENOSPC;
	}

	length = kfifo_in(&priv->msg_fifo, data, len);
	priv->msg_len[priv->msg_idx_wr] = length;
	priv->msg_idx_wr = (priv->msg_idx_wr + 1) % MAX_FIFO_MSG;

	wake_up_interruptible(&priv->wait_list);

	return 0;
}

static int rpmsgcam_probe(struct rpmsg_device *rpdev)
{
	int ret;
	struct rpmsgcam_priv *priv;
	int minor_got;

	dev_dbg(&rpdev->dev, "new channel: 0x%x -> 0x%x!\n", rpdev->src, rpdev->dst);

	priv = devm_kzalloc(&rpdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	mutex_lock(&rpmsgcam_lock);
	minor_got = idr_alloc(&rpmsgcam_minors, priv, 0, PRU_MAX_DEVICES,
			      GFP_KERNEL);
	mutex_unlock(&rpmsgcam_lock);

	if (minor_got < 0) {
		ret = minor_got;
		dev_err(&rpdev->dev,
			"Failed to get a minor number for the rpmsgcam device: %d\n",
			ret);
		goto fail_alloc_minor;
	}

	priv->devt = MKDEV(MAJOR(rpmsgcam_privt), minor_got);

	cdev_init(&priv->cdev, &rpmsgcam_fops);
	priv->cdev.owner = THIS_MODULE;
	ret = cdev_add(&priv->cdev, priv->devt, 1);
	if (ret) {
		dev_err(&rpdev->dev, "Unable to add cdev for the rpmsgcam device\n");
		goto fail_add_cdev;
	}

	priv->dev = device_create(rpmsgcam_class, &rpdev->dev, priv->devt,
				  NULL, "rpmsgcam%d", rpdev->dst);
	if (IS_ERR(priv->dev)) {
		dev_err(&rpdev->dev, "Unable to create the rpmsgcam device\n");
		ret = PTR_ERR(priv->dev);
		goto fail_create_device;
	}

	priv->rpdev = rpdev;

	ret = kfifo_alloc(&priv->msg_fifo, MAX_FIFO_MSG * FIFO_MSG_SIZE,
			  GFP_KERNEL);
	if (ret) {
		dev_err(&rpdev->dev, "Unable to allocate fifo for the rpmsgcam device\n");
		goto fail_alloc_fifo;
	}

	init_waitqueue_head(&priv->wait_list);

	dev_set_drvdata(&rpdev->dev, priv);

	dev_info(&rpdev->dev, "new rpmsg_pru device: /dev/rpmsgcam%d", rpdev->dst);

	/* TODO: module param to autostart camera capture */
	if (0) {
		uint8_t start_cmd[4] = { 0xbe, 0xca, 0x2, 0x0 };
		ret = rpmsg_send(rpdev->ept, start_cmd, sizeof(start_cmd));
		if (ret) {
			dev_err(&rpdev->dev, "rpmsg_send failed: %d\n", ret);
			return ret;
		}
	}

	return 0;

fail_alloc_fifo:
	device_destroy(rpmsgcam_class, priv->devt);
fail_create_device:
	cdev_del(&priv->cdev);
fail_add_cdev:
	mutex_lock(&rpmsgcam_lock);
	idr_remove(&rpmsgcam_minors, minor_got);
	mutex_unlock(&rpmsgcam_lock);
fail_alloc_minor:
	return ret;
}

static void rpmsgcam_remove(struct rpmsg_device *rpdev)
{
	struct rpmsgcam_priv *priv;

	priv = dev_get_drvdata(&rpdev->dev);

	kfifo_free(&priv->msg_fifo);
	device_destroy(rpmsgcam_class, priv->devt);
	cdev_del(&priv->cdev);
	mutex_lock(&rpmsgcam_lock);
	idr_remove(&rpmsgcam_minors, MINOR(priv->devt));
	mutex_unlock(&rpmsgcam_lock);

	dev_dbg(&rpdev->dev, "rpmsgcam driver is removed\n");
}

/* .name matches on RPMsg Channels and causes a probe */
static struct rpmsg_device_id rpmsgcam_id_table[] = {
	{ .name	= "rpmsg-cam" },
	{ },
};
MODULE_DEVICE_TABLE(rpmsg, rpmsgcam_id_table);

static struct rpmsg_driver rpmsgcam_driver = {
	.drv.name	= KBUILD_MODNAME,
	.id_table	= rpmsgcam_id_table,
	.probe		= rpmsgcam_probe,
	.callback	= rpmsgcam_cb,
	.remove		= rpmsgcam_remove,
};

static int __init rpmsgcam_init(void)
{
	int ret;

	rpmsgcam_class = class_create(THIS_MODULE, "rpmsg_cam");
	if (IS_ERR(rpmsgcam_class)) {
		pr_err("Failed to create rpmsg_cam class\n");
		return PTR_ERR(rpmsgcam_class);
	}

	ret = alloc_chrdev_region(&rpmsgcam_privt, 0, PRU_MAX_DEVICES,
				  "rpmsg_cam");
	if (ret) {
		pr_err("Failed to allocate chrdev region\n");
		goto err_alloc_region;
	}

	ret = register_rpmsg_driver(&rpmsgcam_driver);
	if (ret) {
		pr_err("Failed to register rpmsg camera driver");
		goto err_register_driver;
	}

	return 0;

err_register_driver:
	unregister_chrdev_region(rpmsgcam_privt, PRU_MAX_DEVICES);
err_alloc_region:
	class_destroy(rpmsgcam_class);
	return ret;
}

static void __exit rpmsgcam_exit(void)
{
	unregister_rpmsg_driver(&rpmsgcam_driver);
	idr_destroy(&rpmsgcam_minors);
	mutex_destroy(&rpmsgcam_lock);
	class_destroy(rpmsgcam_class);
	unregister_chrdev_region(rpmsgcam_privt, PRU_MAX_DEVICES);
}

module_init(rpmsgcam_init);
module_exit(rpmsgcam_exit);

MODULE_DESCRIPTION("Remote processor messaging camera driver");
MODULE_AUTHOR("Cristian Ciocaltea <cristian.ciocaltea@gmail.com>");
MODULE_LICENSE("GPL");
