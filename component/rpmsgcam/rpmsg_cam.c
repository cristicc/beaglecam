// SPDX-License-Identifier: GPL-2.0-only
/*
 * Remote processor messaging camera driver
 *
 * Copyright (c) 2021 Cristian Ciocaltea <cristian.ciocaltea@gmail.com>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/rpmsg.h>

#define BCAM_CMD_MAGIC			0xbeca
#define BCAM_CMD_START			1
#define BCAM_CMD_STOP			2
#define BCAM_CMD_ACK			3

struct bcam_cmd {
	uint16_t magic;			/* Filter out garbage/corrupted data. */
	uint16_t command;
};

struct rpmsgcam_priv {
	int rx_count;
};

static int rpmsgcam_cb(struct rpmsg_device *rpdev,
		       void *data, int len, void *priv, u32 src)
{
	struct rpmsgcam_priv *rpriv = dev_get_drvdata(&rpdev->dev);

	dev_info(&rpdev->dev, "incoming msg %d (src: 0x%x)\n",
		 ++rpriv->rx_count, src);

	print_hex_dump_debug(__func__, DUMP_PREFIX_NONE, 16, 1, data, len, true);

	return 0;
}

static int rpmsgcam_probe(struct rpmsg_device *rpdev)
{
	int ret;
	struct rpmsgcam_priv *rpriv;

	struct bcam_cmd start_cmd = { BCAM_CMD_MAGIC, BCAM_CMD_START };

	dev_info(&rpdev->dev, "new channel: 0x%x -> 0x%x!\n", rpdev->src, rpdev->dst);

	rpriv = devm_kzalloc(&rpdev->dev, sizeof(*rpriv), GFP_KERNEL);
	if (!rpriv)
		return -ENOMEM;

	dev_set_drvdata(&rpdev->dev, rpriv);

	ret = rpmsg_send(rpdev->ept, &start_cmd, sizeof(start_cmd));
	if (ret) {
		dev_err(&rpdev->dev, "rpmsg_send failed: %d\n", ret);
		return ret;
	}

	return 0;
}

static void rpmsgcam_remove(struct rpmsg_device *rpdev)
{
	dev_info(&rpdev->dev, "rpmsg cam driver is removed\n");
}

static struct rpmsg_device_id rpmsgcam_id_table[] = {
	{ .name	= "rpmsg-cam" },
	{ },
};
MODULE_DEVICE_TABLE(rpmsg, rpmsgcam_id_table);

static struct rpmsg_driver rpmsgcam_client = {
	.drv.name	= KBUILD_MODNAME,
	.id_table	= rpmsgcam_id_table,
	.probe		= rpmsgcam_probe,
	.callback	= rpmsgcam_cb,
	.remove		= rpmsgcam_remove,
};
module_rpmsg_driver(rpmsgcam_client);

MODULE_DESCRIPTION("Remote processor messaging camera driver");
MODULE_LICENSE("GPL v2");
