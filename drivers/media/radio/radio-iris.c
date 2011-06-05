/* Copyright (c) 2011, Code Aurora Forum. All rights reserved
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#define DRIVER_AUTHOR "Archana Ramchandran <archanar@codeaurora.org>"
#define DRIVER_NAME "radio-iris"
#define DRIVER_CARD "Qualcomm FM Radio Transceiver"
#define DRIVER_DESC "Driver for Qualcomm FM Radio Transceiver "

#include <linux/version.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/uaccess.h>
#include <linux/kfifo.h>
#include <linux/param.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/version.h>
#include <linux/videodev2.h>
#include <linux/mutex.h>
#include <linux/unistd.h>
#include <linux/atomic.h>
#include <linux/platform_device.h>
#include <linux/workqueue.h>
#include <linux/slab.h>
#include <media/v4l2-common.h>
#include <media/v4l2-ioctl.h>
#include <media/radio-iris.h>
#include <asm/unaligned.h>

static unsigned int rds_buf = 100;
module_param(rds_buf, uint, 0);
MODULE_PARM_DESC(rds_buf, "RDS buffer entries: *100*");

static void radio_hci_cmd_task(unsigned long arg);
static void radio_hci_rx_task(unsigned long arg);
static struct video_device *video_get_dev(void);
static DEFINE_RWLOCK(hci_task_lock);

struct iris_device {
	struct device *dev;
	struct kfifo data_buf[IRIS_BUF_MAX];

	int pending_xfrs[IRIS_XFR_MAX];
	int xfr_bytes_left;
	int xfr_in_progress;
	struct completion sync_xfr_start;
	int tune_req;

	struct video_device *videodev;

	struct mutex lock;
	spinlock_t buf_lock[IRIS_BUF_MAX];
	wait_queue_head_t event_queue;
	wait_queue_head_t read_queue;

	struct hci_dev *fm_hdev;

	struct v4l2_capability *g_cap;
	struct v4l2_control *g_ctl;

	struct hci_fm_mute_mode_req mute_mode;
	struct hci_fm_stereo_mode_req stereo_mode;
	struct hci_fm_station_rsp fm_st_rsp;
	struct hci_fm_search_station_req srch_st;
	struct hci_fm_search_rds_station_req srch_rds;
	struct hci_fm_search_station_list_req srch_st_list;
	struct hci_fm_recv_conf_req recv_conf;
	struct hci_fm_rds_grp_req rds_grp;
	unsigned char g_search_mode;
	unsigned char g_scan_time;
	unsigned int g_antenna;
	unsigned int g_rds_grp_proc_ps;
	enum iris_region_t region;
	struct hci_fm_dbg_param_rsp st_dbg_param;
	struct hci_ev_srch_list_compl srch_st_result;
};

static struct v4l2_queryctrl iris_v4l2_queryctrl[] = {
	{
	.id	= V4L2_CID_AUDIO_VOLUME,
	.type	= V4L2_CTRL_TYPE_INTEGER,
	.name	= "Volume",
	.minimum	= 0,
	.maximum	= 15,
	.step	=	1,
	.default_value	=	15,
	},
	{
	.id	=	V4L2_CID_AUDIO_BALANCE,
	.flags	= V4L2_CTRL_FLAG_DISABLED,
	},
	{
	.id	=	V4L2_CID_AUDIO_BASS,
	.flags	=	V4L2_CTRL_FLAG_DISABLED,
	},
	{
	.id	=	V4L2_CID_AUDIO_TREBLE,
	.flags	=	V4L2_CTRL_FLAG_DISABLED,
	},
	{
	.id	=	V4L2_CID_AUDIO_MUTE,
	.type	=	V4L2_CTRL_TYPE_BOOLEAN,
	.name	=	"Mute",
	.minimum	=	0,
	.maximum	=	1,
	.step	=	1,
	.default_value	= 1,
	},
	{
	.id	=	V4L2_CID_AUDIO_LOUDNESS,
	.flags	=	V4L2_CTRL_FLAG_DISABLED,
	},
	{
	.id	=	V4L2_CID_PRIVATE_IRIS_SRCHMODE,
	.type	=	V4L2_CTRL_TYPE_INTEGER,
	.name	=	"Search mode",
	.minimum	=	0,
	.maximum	= 7,
	.step	= 1,
	.default_value	= 0,
	},
	{
	.id	= V4L2_CID_PRIVATE_IRIS_SCANDWELL,
	.type	= V4L2_CTRL_TYPE_INTEGER,
	.name	=	"Search dwell time",
	.minimum	= 0,
	.maximum	= 7,
	.step	= 1,
	.default_value	= 0,
	},
	{
	.id	= V4L2_CID_PRIVATE_IRIS_SRCHON,
	.type	= V4L2_CTRL_TYPE_BOOLEAN,
	.name	= "Search on/off",
	.minimum	= 0,
	.maximum	= 1,
	.step	= 1,
	.default_value	= 1,

	},
	{
	.id	=	V4L2_CID_PRIVATE_IRIS_STATE,
	.type	= V4L2_CTRL_TYPE_INTEGER,
	.name	= "radio 0ff/rx/tx/reset",
	.minimum	= 0,
	.maximum	= 3,
	.step	= 1,
	.default_value	=	1,

	},
	{
	.id	= V4L2_CID_PRIVATE_IRIS_REGION,
	.type	= V4L2_CTRL_TYPE_INTEGER,
	.name	=	"radio standard",
	.minimum	=	0,
	.maximum	=	2,
	.step	=	1,
	.default_value	=	0,
	},
	{
	.id	=	V4L2_CID_PRIVATE_IRIS_SIGNAL_TH,
	.type	=	V4L2_CTRL_TYPE_INTEGER,
	.name	=	"Signal Threshold",
	.minimum	=	0x80,
	.maximum	=	0x7F,
	.step	=	1,
	.default_value	=	0,
	},
	{
	.id	=	V4L2_CID_PRIVATE_IRIS_SRCH_PTY,
	.type	=	V4L2_CTRL_TYPE_INTEGER,
	.name	=	"Search PTY",
	.minimum	=	0,
	.maximum	=	31,
	.default_value	=	0,
	},
	{
	.id	=	V4L2_CID_PRIVATE_IRIS_SRCH_PI,
	.type	=	V4L2_CTRL_TYPE_INTEGER,
	.name	=	"Search PI",
	.minimum	=	0,
	.maximum	=	0xFF,
	.default_value	=	0,
	},
	{
	.id	=	V4L2_CID_PRIVATE_IRIS_SRCH_CNT,
	.type	=	V4L2_CTRL_TYPE_INTEGER,
	.name	=	"Preset num",
	.minimum	=	0,
	.maximum	=	12,
	.default_value	=	0,
	},
	{
	.id	=	V4L2_CID_PRIVATE_IRIS_EMPHASIS,
	.type	=	V4L2_CTRL_TYPE_BOOLEAN,
	.name	=	"Emphasis",
	.minimum	=	0,
	.maximum	=	1,
	.default_value	=	0,
	},
	{
	.id	=	V4L2_CID_PRIVATE_IRIS_RDS_STD,
	.type	=	V4L2_CTRL_TYPE_BOOLEAN,
	.name	=	"RDS standard",
	.minimum	=	0,
	.maximum	=	1,
	.default_value	=	0,
	},
	{
	.id	=	V4L2_CID_PRIVATE_IRIS_SPACING,
	.type	=	V4L2_CTRL_TYPE_INTEGER,
	.name	=	"Channel spacing",
	.minimum	=	0,
	.maximum	=	2,
	.default_value	=	0,
	},
	{
	.id	=	V4L2_CID_PRIVATE_IRIS_RDSON,
	.type	=	V4L2_CTRL_TYPE_BOOLEAN,
	.name	=	"RDS on/off",
	.minimum	=	0,
	.maximum	=	1,
	.default_value	=	0,
	},
	{
	.id	=	V4L2_CID_PRIVATE_IRIS_RDSGROUP_MASK,
	.type	=	V4L2_CTRL_TYPE_INTEGER,
	.name	=	"RDS group mask",
	.minimum	=	0,
	.maximum	=	0xFFFFFFFF,
	.default_value	=	0,
	},
	{
	.id	=	V4L2_CID_PRIVATE_IRIS_RDSGROUP_PROC,
	.type	=	V4L2_CTRL_TYPE_INTEGER,
	.name	=	"RDS processing",
	.minimum	=	0,
	.maximum	=	0xFF,
	.default_value	=	0,
	},
	{
	.id	=	V4L2_CID_PRIVATE_IRIS_RDSD_BUF,
	.type	=	V4L2_CTRL_TYPE_INTEGER,
	.name	=	"RDS data groups to buffer",
	.minimum	=	1,
	.maximum	=	21,
	.default_value	=	0,
	},
	{
	.id	=	V4L2_CID_PRIVATE_IRIS_PSALL,
	.type	=	V4L2_CTRL_TYPE_BOOLEAN,
	.name	=	"pass all ps strings",
	.minimum	=	0,
	.maximum	=	1,
	.default_value	=	0,
	},
	{
	.id	=	V4L2_CID_PRIVATE_IRIS_LP_MODE,
	.type	=	V4L2_CTRL_TYPE_BOOLEAN,
	.name	=	"Low power mode",
	.minimum	=	0,
	.maximum	=	1,
	.default_value	=	0,
	},
	{
	.id	=	V4L2_CID_PRIVATE_IRIS_ANTENNA,
	.type	=	V4L2_CTRL_TYPE_BOOLEAN,
	.name	=	"headset/internal",
	.minimum	=	0,
	.maximum	=	1,
	.default_value	=	0,
	},

	{
	.id	=	V4L2_CID_PRIVATE_IRIS_TX_SETPSREPEATCOUNT,
	.type	=	V4L2_CTRL_TYPE_INTEGER,
	.name	=	"Set PS REPEATCOUNT",
	.minimum	=	0,
	.maximum	=	15,
	},
	{
	.id	=	V4L2_CID_PRIVATE_IRIS_STOP_RDS_TX_PS_NAME,
	.type	=	V4L2_CTRL_TYPE_BOOLEAN,
	.name	=	"Stop PS NAME",
	.minimum	=	0,
	.maximum	=	1,
	},
	{
	.id	=	V4L2_CID_PRIVATE_IRIS_STOP_RDS_TX_RT,
	.type	=	V4L2_CTRL_TYPE_BOOLEAN,
	.name	=	"Stop RT",
	.minimum	=	0,
	.maximum	=	1,
	},

};

static void iris_q_event(struct iris_device *radio,
				enum iris_evt_t event)
{
	struct kfifo *data_b = &radio->data_buf[IRIS_BUF_EVENTS];
	unsigned char evt = event;
	if (kfifo_in_locked(data_b, &evt, 1, &radio->buf_lock[IRIS_BUF_EVENTS]))
		wake_up_interruptible(&radio->event_queue);
}

static int hci_send_frame(struct sk_buff *skb)
{
	struct hci_dev *hdev = (struct hci_dev *) skb->dev;

	if (!hdev) {
		kfree_skb(skb);
		return -ENODEV;
	}

	__net_timestamp(skb);

	skb_orphan(skb);
	return hdev->send(skb);
}

static void radio_hci_cmd_task(unsigned long arg)
{
	struct hci_dev *hdev = (struct hci_dev *) arg;
	struct sk_buff *skb;
	if (!(atomic_read(&hdev->cmd_cnt))
		&& time_after(jiffies, hdev->cmd_last_tx + HZ)) {
		FMDERR("%s command tx timeout", hdev->name);
		atomic_set(&hdev->cmd_cnt, 1);
	}

	skb = skb_dequeue(&hdev->cmd_q);
	if (atomic_read(&hdev->cmd_cnt) && skb) {
		kfree_skb(hdev->sent_cmd);
		hdev->sent_cmd = skb_clone(skb, GFP_ATOMIC);
		if (hdev->sent_cmd) {
			atomic_dec(&hdev->cmd_cnt);
			hci_send_frame(skb);
			hdev->cmd_last_tx = jiffies;
		} else {
			skb_queue_head(&hdev->cmd_q, skb);
			tasklet_schedule(&hdev->cmd_task);
		}
	}

}

static void radio_hci_rx_task(unsigned long arg)
{
	struct hci_dev *hdev = (struct hci_dev *) arg;
	struct sk_buff *skb;

	read_lock(&hci_task_lock);

	skb = skb_dequeue(&hdev->rx_q);
	radio_hci_event_packet(hdev, skb);

	read_unlock(&hci_task_lock);
}

int radio_hci_register_dev(struct hci_dev *hdev)
{
	struct iris_device *radio = video_get_drvdata(video_get_dev());
	struct hci_dev *dev;

	if (!hdev->open || !hdev->close || !hdev->destruct)
		return -EINVAL;

	dev = hdev;
	dev->flags = 0;

	tasklet_init(&dev->cmd_task, radio_hci_cmd_task, (unsigned long)
		hdev);
	tasklet_init(&dev->rx_task, radio_hci_rx_task, (unsigned long)
		hdev);

	skb_queue_head_init(&dev->rx_q);
	skb_queue_head_init(&dev->cmd_q);
	skb_queue_head_init(&dev->raw_q);

	radio->fm_hdev = dev;

	return 0;
}

int radio_hci_unregister_dev(struct hci_dev *hdev)
{
	tasklet_kill(&hdev->rx_task);
	tasklet_kill(&hdev->cmd_task);
	skb_queue_purge(&hdev->rx_q);
	skb_queue_purge(&hdev->cmd_q);
	skb_queue_purge(&hdev->raw_q);
	return 0;
}

int radio_hci_recv_frame(struct sk_buff *skb)
{
	struct hci_dev *hdev = (struct hci_dev *) skb->dev;
	if (!hdev) {
		FMDERR("%s hdev is null while receiving frame", hdev->name);
		kfree_skb(skb);
		return -ENXIO;
	}

	__net_timestamp(skb);

	skb_queue_tail(&hdev->rx_q, skb);
	tasklet_schedule(&hdev->rx_task);

	return 0;
}

int radio_hci_send_cmd(struct hci_dev *hdev, __u16 opcode, __u32 plen,
		void *param)
{
	int len = RADIO_HCI_COMMAND_HDR_SIZE + plen;
	struct radio_hci_command_hdr *hdr;
	struct sk_buff *skb;

	skb = alloc_skb(len, GFP_ATOMIC);
	if (!skb) {
		FMDERR("%s no memory for command", hdev->name);
		return -ENOMEM;
	}

	hdr = (struct radio_hci_command_hdr *) skb_put(skb,
		RADIO_HCI_COMMAND_HDR_SIZE);
	hdr->protocol_byte = RADIO_HCI_COMMAND_PKT;
	hdr->opcode = cpu_to_le16(opcode);
	hdr->plen   = plen;

	if (plen)
		memcpy(skb_put(skb, plen), param, plen);

	skb->dev = (void *) hdev;

	skb_queue_tail(&hdev->cmd_q, skb);
	tasklet_schedule(&hdev->cmd_task);
	return 0;
}

static void hci_fm_enable_recv_req(struct hci_dev *hdev, unsigned long param)
{
	__u16 opcode = 0;

	opcode = hci_opcode_pack(HCI_OGF_FM_RECV_CTRL_CMD_REQ,
		HCI_OCF_FM_ENABLE_RECV_REQ);
	radio_hci_send_cmd(hdev, opcode, 0, NULL);
}

static void hci_fm_disable_recv_req(struct hci_dev *hdev, unsigned long param)
{
	__u16 opcode = 0;

	opcode = hci_opcode_pack(HCI_OGF_FM_RECV_CTRL_CMD_REQ,
		HCI_OCF_FM_DISABLE_RECV_REQ);
	radio_hci_send_cmd(hdev, opcode, 0, NULL);
}

static void hci_get_fm_recv_conf_req(struct hci_dev *hdev, unsigned long param)
{
	__u16 opcode = 0;

	opcode = hci_opcode_pack(HCI_OGF_FM_RECV_CTRL_CMD_REQ,
		HCI_OCF_FM_GET_RECV_CONF_REQ);
	radio_hci_send_cmd(hdev, opcode, 0, NULL);
}

static void hci_set_fm_recv_conf_req(struct hci_dev *hdev, unsigned long param)
{
	__u16 opcode = 0;

	struct hci_fm_recv_conf_req *recv_conf_req =
		(struct hci_fm_recv_conf_req *) param;

	opcode = hci_opcode_pack(HCI_OGF_FM_RECV_CTRL_CMD_REQ,
		HCI_OCF_FM_SET_RECV_CONF_REQ);
	radio_hci_send_cmd(hdev, opcode, sizeof((*recv_conf_req)),
		recv_conf_req);
}

static void hci_fm_get_station_param_req(struct hci_dev *hdev,
		unsigned long param)
{
	__u16 opcode = 0;

	opcode = hci_opcode_pack(HCI_OGF_FM_RECV_CTRL_CMD_REQ,
		HCI_OCF_FM_GET_STATION_PARAM_REQ);
	radio_hci_send_cmd(hdev, opcode, 0, NULL);
}

static void hci_set_fm_mute_mode_req(struct hci_dev *hdev,
		unsigned long param)
{
	__u16 opcode = 0;
	struct hci_fm_mute_mode_req *mute_mode_req =
		(struct hci_fm_mute_mode_req *) param;

	opcode = hci_opcode_pack(HCI_OGF_FM_RECV_CTRL_CMD_REQ,
		HCI_OCF_FM_SET_MUTE_MODE_REQ);
	radio_hci_send_cmd(hdev, opcode, sizeof((*mute_mode_req)),
		mute_mode_req);
}

static void hci_set_fm_stereo_mode_req(struct hci_dev *hdev,
		unsigned long param)
{
	__u16 opcode = 0;
	struct hci_fm_stereo_mode_req *stereo_mode_req =
		(struct hci_fm_stereo_mode_req *) param;
	opcode = hci_opcode_pack(HCI_OGF_FM_RECV_CTRL_CMD_REQ,
		HCI_OCF_FM_SET_STEREO_MODE_REQ);
	radio_hci_send_cmd(hdev, opcode, sizeof((*stereo_mode_req)),
		stereo_mode_req);
}

static void hci_fm_set_antenna_req(struct hci_dev *hdev, unsigned long param)
{
	__u16 opcode = 0;

	__u8 antenna = param;

	opcode = hci_opcode_pack(HCI_OGF_FM_COMMON_CTRL_CMD_REQ,
		HCI_OCF_FM_SET_ANTENNA);
	radio_hci_send_cmd(hdev, opcode, sizeof(antenna), &antenna);
}

static void hci_fm_set_sig_threshold_req(struct hci_dev *hdev,
		unsigned long param)
{
	__u16 opcode = 0;

	__u8 sig_threshold = param;

	opcode = hci_opcode_pack(HCI_OGF_FM_COMMON_CTRL_CMD_REQ,
		HCI_OCF_FM_SET_SIGNAL_THRESHOLD);
	radio_hci_send_cmd(hdev, opcode, sizeof(sig_threshold),
		&sig_threshold);
}

static void hci_fm_get_sig_threshold_req(struct hci_dev *hdev,
		unsigned long param)
{
	__u16 opcode = 0;

	opcode = hci_opcode_pack(HCI_OGF_FM_COMMON_CTRL_CMD_REQ,
		HCI_OCF_FM_GET_SIGNAL_THRESHOLD);
	radio_hci_send_cmd(hdev, opcode, 0, NULL);
}

static void hci_fm_get_program_service_req(struct hci_dev *hdev,
		unsigned long param)
{
	__u16 opcode = 0;

	opcode = hci_opcode_pack(HCI_OGF_FM_COMMON_CTRL_CMD_REQ,
		HCI_OCF_FM_GET_PROGRAM_SERVICE_REQ);
	radio_hci_send_cmd(hdev, opcode, 0, NULL);
}

static void hci_fm_get_radio_text_req(struct hci_dev *hdev,
		unsigned long param)
{
	__u16 opcode = 0;

	opcode = hci_opcode_pack(HCI_OGF_FM_COMMON_CTRL_CMD_REQ,
		HCI_OCF_FM_GET_RADIO_TEXT_REQ);
	radio_hci_send_cmd(hdev, opcode, 0, NULL);
}

static void hci_fm_get_af_list_req(struct hci_dev *hdev, unsigned long param)
{
	__u16 opcode = 0;

	opcode = hci_opcode_pack(HCI_OGF_FM_COMMON_CTRL_CMD_REQ,
		HCI_OCF_FM_GET_AF_LIST_REQ);
	radio_hci_send_cmd(hdev, opcode, 0, NULL);
}

static void hci_fm_search_stations_req(struct hci_dev *hdev,
		unsigned long param)
{
	__u16 opcode = 0;
	struct hci_fm_search_station_req *srch_stations =
		(struct hci_fm_search_station_req *) param;

	opcode = hci_opcode_pack(HCI_OGF_FM_RECV_CTRL_CMD_REQ,
		HCI_OCF_FM_TUNE_STATION_REQ);
	radio_hci_send_cmd(hdev, opcode, sizeof((*srch_stations)),
		srch_stations);
}

static void hci_fm_srch_rds_stations_req(struct hci_dev *hdev,
		unsigned long param)
{
	__u16 opcode = 0;
	struct hci_fm_search_rds_station_req *srch_stations =
		(struct hci_fm_search_rds_station_req *) param;

	opcode = hci_opcode_pack(HCI_OGF_FM_RECV_CTRL_CMD_REQ,
		HCI_OCF_FM_SEARCH_RDS_STATIONS);
	radio_hci_send_cmd(hdev, opcode, sizeof((*srch_stations)),
		srch_stations);
}

static void hci_fm_srch_station_list_req(struct hci_dev *hdev,
		unsigned long param)
{
	__u16 opcode = 0;
	struct hci_fm_search_station_list_req *srch_list =
		(struct hci_fm_search_station_list_req *) param;

	opcode = hci_opcode_pack(HCI_OGF_FM_RECV_CTRL_CMD_REQ,
		HCI_OCF_FM_SEARCH_STATIONS_LIST);
	radio_hci_send_cmd(hdev, opcode, sizeof((*srch_list)), srch_list);
}

static void hci_fm_cancel_search_req(struct hci_dev *hdev,
		unsigned long param)
{
	__u16 opcode = 0;

	opcode = hci_opcode_pack(HCI_OGF_FM_COMMON_CTRL_CMD_REQ,
		HCI_OCF_FM_CANCEL_SEARCH);
	radio_hci_send_cmd(hdev, opcode, 0, NULL);
}

static void hci_fm_rds_grp_req(struct hci_dev *hdev, unsigned long param)
{
	__u16 opcode = 0;
	struct hci_fm_rds_grp_req *rds_grp = (struct hci_fm_rds_grp_req *)
		param;

	opcode = hci_opcode_pack(HCI_OGF_FM_RECV_CTRL_CMD_REQ,
		HCI_OCF_FM_RDS_GRP);
	radio_hci_send_cmd(hdev, opcode, sizeof((*rds_grp)), rds_grp);
}

static void hci_fm_rds_grp_process_req(struct hci_dev *hdev,
		unsigned long param)
{
	__u16 opcode = 0;

	__u32 fm_grps_process = param;

	opcode = hci_opcode_pack(HCI_OGF_FM_COMMON_CTRL_CMD_REQ,
		HCI_OCF_FM_RDS_GRP_PROCESS);
	radio_hci_send_cmd(hdev, opcode, sizeof(fm_grps_process),
		&fm_grps_process);
}

static void hci_fm_tune_station_req(struct hci_dev *hdev, unsigned long param)
{
	__u16 opcode = 0;

	__u32 tune_freq = param;

	opcode = hci_opcode_pack(HCI_OGF_FM_COMMON_CTRL_CMD_REQ,
		HCI_OCF_FM_TUNE_STATION_REQ);
	radio_hci_send_cmd(hdev, opcode, sizeof(tune_freq), &tune_freq);
}

static void hci_def_data_read_req(struct hci_dev *hdev, unsigned long param)
{
	__u16 opcode = 0;
	struct hci_fm_def_data_rd_req *def_data_rd =
		(struct hci_fm_def_data_rd_req *) param;

	opcode = hci_opcode_pack(HCI_OGF_FM_RECV_CTRL_CMD_REQ,
		HCI_OCF_FM_DEFAULT_DATA_READ);
	radio_hci_send_cmd(hdev, opcode, sizeof((*def_data_rd)), def_data_rd);
}

static void hci_def_data_write_req(struct hci_dev *hdev, unsigned long param)
{
	__u16 opcode = 0;
	struct hci_fm_def_data_wr_req *def_data_wr =
		(struct hci_fm_def_data_wr_req *) param;

	opcode = hci_opcode_pack(HCI_OGF_FM_RECV_CTRL_CMD_REQ,
		HCI_OCF_FM_DEFAULT_DATA_WRITE);
	radio_hci_send_cmd(hdev, opcode, sizeof((*def_data_wr)), def_data_wr);
}

static void hci_fm_reset_req(struct hci_dev *hdev, unsigned long param)
{
	__u16 opcode = 0;

	opcode = hci_opcode_pack(HCI_OGF_FM_COMMON_CTRL_CMD_REQ,
		HCI_OCF_FM_RESET);
	radio_hci_send_cmd(hdev, opcode, 0, NULL);
}

static void hci_fm_get_feature_lists_req(struct hci_dev *hdev,
		unsigned long param)
{
	__u16 opcode = 0;

	opcode = hci_opcode_pack(HCI_OGF_FM_COMMON_CTRL_CMD_REQ,
		HCI_OCF_FM_GET_FEATURE_LIST);
	radio_hci_send_cmd(hdev, opcode, 0, NULL);
}

static void hci_fm_do_calibration_req(struct hci_dev *hdev,
		unsigned long param)
{
	__u16 opcode = 0;

	__u8 mode = param;

	opcode = hci_opcode_pack(HCI_OGF_FM_COMMON_CTRL_CMD_REQ,
		HCI_OCF_FM_DO_CALIBRATION);
	radio_hci_send_cmd(hdev, opcode, sizeof(mode), &mode);
}

static void hci_read_grp_counters_req(struct hci_dev *hdev,
		unsigned long param)
{
	__u16 opcode = 0;

	__u8 reset_counters = param;

	opcode = hci_opcode_pack(HCI_OGF_FM_COMMON_CTRL_CMD_REQ,
		HCI_OCF_FM_READ_GRP_COUNTERS);
	radio_hci_send_cmd(hdev, opcode, sizeof(reset_counters),
		&reset_counters);
}

static void hci_peek_data_req(struct hci_dev *hdev, unsigned long param)
{
	__u16 opcode = 0;
	struct hci_fm_peek_req *peek_data = (struct hci_fm_peek_req *) param;

	opcode = hci_opcode_pack(HCI_OGF_FM_RECV_CTRL_CMD_REQ,
		HCI_OCF_FM_PEEK_DATA);
	radio_hci_send_cmd(hdev, opcode, sizeof((*peek_data)), peek_data);
}

static void hci_poke_data_req(struct hci_dev *hdev, unsigned long param)
{
	__u16 opcode = 0;
	struct hci_fm_poke_req *poke_data = (struct hci_fm_poke_req *) param;

	opcode = hci_opcode_pack(HCI_OGF_FM_RECV_CTRL_CMD_REQ,
		HCI_OCF_FM_POKE_DATA);
	radio_hci_send_cmd(hdev, opcode, sizeof((*poke_data)), poke_data);
}

static void hci_ssbi_peek_reg_req(struct hci_dev *hdev, unsigned long param)
{
	__u16 opcode = 0;
	struct hci_fm_ssbi_req *ssbi_peek = (struct hci_fm_ssbi_req *) param;

	opcode = hci_opcode_pack(HCI_OGF_FM_RECV_CTRL_CMD_REQ,
		HCI_OCF_FM_SSBI_PEEK_REG);
	radio_hci_send_cmd(hdev, opcode, sizeof((*ssbi_peek)), ssbi_peek);
}

static void hci_ssbi_poke_reg_req(struct hci_dev *hdev, unsigned long param)
{
	__u16 opcode = 0;
	struct hci_fm_ssbi_req *ssbi_poke = (struct hci_fm_ssbi_req *) param;

	opcode = hci_opcode_pack(HCI_OGF_FM_RECV_CTRL_CMD_REQ,
		HCI_OCF_FM_SSBI_POKE_REG);
	radio_hci_send_cmd(hdev, opcode, sizeof((*ssbi_poke)), ssbi_poke);
}

static void hci_fm_get_station_dbg_param_req(struct hci_dev *hdev,
		unsigned long param)
{
	__u16 opcode = 0;

	opcode = hci_opcode_pack(HCI_OGF_FM_COMMON_CTRL_CMD_REQ,
		HCI_OCF_FM_STATION_DBG_PARAM);
	radio_hci_send_cmd(hdev, opcode, 0, NULL);
}

static int radio_hci_err(__u16 code)
{
	switch (code) {
	case 0:
		return 0;
	case 0x01:
		return -EBADRQC;
	case 0x02:
		return -ENOTCONN;
	case 0x03:
		return -EIO;
	case 0x07:
		return -ENOMEM;
	case 0x0c:
		return -EBUSY;
	case 0x11:
		return -EOPNOTSUPP;
	case 0x12:
		return -EINVAL;
	default:
		return -ENOSYS;
	}
}

static int __radio_hci_request(struct hci_dev *hdev,
		void (*req)(struct	hci_dev *hdev, unsigned long param),
			unsigned long param, __u32 timeout)
{
	DECLARE_WAITQUEUE(wait, current);
	int err = 0;

	hdev->req_status = HCI_REQ_PEND;

	add_wait_queue(&hdev->req_wait_q, &wait);
	set_current_state(TASK_INTERRUPTIBLE);

	/* Execute the request*/
	req(hdev, param);
	schedule_timeout(timeout);

	remove_wait_queue(&hdev->req_wait_q, &wait);

	if (signal_pending(current))
		return -EINTR;

	switch (hdev->req_status) {
	case HCI_REQ_DONE:
	case HCI_REQ_STATUS:
		err = radio_hci_err(hdev->req_result);
		break;

	case HCI_REQ_CANCELED:
		err = -hdev->req_result;
		break;

	default:
		err = -ETIMEDOUT;
		break;
	}

	hdev->req_status = hdev->req_result = 0;

	return err;
}

static inline int radio_hci_request(struct hci_dev *hdev, void (*req)(struct
		hci_dev * hdev, unsigned long param),
		unsigned long param, __u32 timeout)
{
	int ret = 0;

	hci_req_lock(hdev);
	ret = __radio_hci_request(hdev, req, param, timeout);
	hci_req_unlock(hdev);

	return ret;
}

static int hci_set_fm_recv_conf(void __user *arg,
		struct hci_dev *hdev)
{
	__u8 __user *ptr = arg;
	int ret = 0;
	struct hci_fm_recv_conf_req set_recv_conf;

	if (copy_from_user(&set_recv_conf, ptr, sizeof(set_recv_conf)))
		return -EFAULT;

	ret = radio_hci_request(hdev, hci_set_fm_recv_conf_req, (unsigned
		long)&set_recv_conf, RADIO_HCI_TIMEOUT);

	return ret;
}

static int hci_fm_tune_station(void __user *arg, struct hci_dev *hdev)
{
	__u8 __user *ptr = arg;
	int ret = 0;
	__u32 tune_freq;

	if (copy_from_user(&tune_freq, ptr, sizeof(tune_freq)))
		return -EFAULT;

	ret = radio_hci_request(hdev, hci_fm_tune_station_req, tune_freq,
		RADIO_HCI_TIMEOUT);

	return ret;
}

static int hci_set_fm_mute_mode(void __user *arg, struct hci_dev *hdev)
{
	__u8 __user *ptr = arg;
	int ret = 0;
	struct hci_fm_mute_mode_req set_mute_conf;

	if (copy_from_user(&set_mute_conf, ptr, sizeof(set_mute_conf)))
		return -EFAULT;

	ret = radio_hci_request(hdev, hci_set_fm_mute_mode_req, (unsigned
		long)&set_mute_conf, RADIO_HCI_TIMEOUT);

	return ret;
}

static int hci_set_fm_stereo_mode(void __user *arg, struct hci_dev *hdev)
{
	__u8 __user *ptr = arg;
	int ret = 0;
	struct hci_fm_stereo_mode_req set_stereo_conf;

	if (copy_from_user(&set_stereo_conf, ptr, sizeof(set_stereo_conf)))
		return -EFAULT;

	ret = radio_hci_request(hdev, hci_set_fm_stereo_mode_req, (unsigned
		long)&set_stereo_conf, RADIO_HCI_TIMEOUT);

	return ret;
}

static int hci_fm_set_antenna(void __user *arg, struct hci_dev *hdev)
{
	__u8 __user *ptr = arg;
	int ret = 0;
	__u8 antenna;

	if (copy_from_user(&antenna, ptr, sizeof(antenna)))
		return -EFAULT;

	ret = radio_hci_request(hdev, hci_fm_set_antenna_req, antenna,
		RADIO_HCI_TIMEOUT);

	return ret;
}

static int hci_fm_set_signal_threshold(void __user *arg, struct hci_dev *hdev)
{
	__u8 __user *ptr = arg;
	int ret = 0;
	__u8 sig_threshold;

	if (copy_from_user(&sig_threshold, ptr, sizeof(sig_threshold)))
		return -EFAULT;

	ret = radio_hci_request(hdev, hci_fm_set_sig_threshold_req,
		sig_threshold, RADIO_HCI_TIMEOUT);

	return ret;
}

static int hci_fm_search_stations(void __user *arg, struct hci_dev *hdev)
{
	__u8 __user *ptr = arg;
	int ret = 0;
	struct hci_fm_search_station_req srch_stations;

	if (copy_from_user(&srch_stations, ptr, sizeof(srch_stations)))
		return -EFAULT;

	ret = radio_hci_request(hdev, hci_fm_search_stations_req, (unsigned
		long)&srch_stations, RADIO_HCI_TIMEOUT);

	return ret;
}

static int hci_fm_search_rds_stations(void __user *arg, struct hci_dev *hdev)
{
	__u8 __user *ptr = arg;
	int ret = 0;
	struct hci_fm_search_rds_station_req srch_stations;

	if (copy_from_user(&srch_stations, ptr, sizeof(srch_stations)))
		return -EFAULT;

	ret = radio_hci_request(hdev, hci_fm_srch_rds_stations_req, (unsigned
		long)&srch_stations, RADIO_HCI_TIMEOUT);

	return ret;
}

static int hci_fm_search_station_list(void __user *arg, struct hci_dev *hdev)
{
	__u8 __user *ptr = arg;
	int ret = 0;
	struct hci_fm_search_station_list_req srch_list;

	if (copy_from_user(&srch_list, ptr, sizeof(srch_list)))
		return -EFAULT;

	ret = radio_hci_request(hdev, hci_fm_srch_station_list_req, (unsigned
		long)&srch_list, RADIO_HCI_TIMEOUT);

	return ret;
}

static int hci_fm_rds_grp(void __user *arg, struct hci_dev *hdev)
{
	__u8 __user *ptr = arg;
	int ret = 0;
	struct hci_fm_rds_grp_req rds_grp;

	if (copy_from_user(&rds_grp, ptr, sizeof(rds_grp)))
		return -EFAULT;

	ret = radio_hci_request(hdev, hci_fm_rds_grp_req, (unsigned
		long)&rds_grp, RADIO_HCI_TIMEOUT);

	return ret;
}

static int hci_fm_rds_grps_process(void __user *arg, struct hci_dev *hdev)
{
	__u8 __user *ptr = arg;
	int ret = 0;
	__u32 fm_grps_process;

	if (copy_from_user(&fm_grps_process, ptr, sizeof(fm_grps_process)))
		return -EFAULT;

	ret = radio_hci_request(hdev, hci_fm_rds_grp_process_req,
		fm_grps_process, RADIO_HCI_TIMEOUT);

	return ret;
}

int hci_def_data_read(void __user *arg, struct hci_dev *hdev)
{
	__u8 __user *ptr = arg;
	int ret = 0;
	struct hci_fm_def_data_rd_req def_data_rd;

	if (copy_from_user(&def_data_rd, ptr, sizeof(def_data_rd)))
		return -EFAULT;

	ret = radio_hci_request(hdev, hci_def_data_read_req, (unsigned
		long)&def_data_rd, RADIO_HCI_TIMEOUT);

	return ret;
}

int hci_def_data_write(void __user *arg, struct hci_dev *hdev)
{
	__u8 __user *ptr = arg;
	int ret = 0;
	struct hci_fm_def_data_wr_req def_data_wr;

	if (copy_from_user(&def_data_wr, ptr, sizeof(def_data_wr)))
		return -EFAULT;

	ret = radio_hci_request(hdev, hci_def_data_write_req, (unsigned
		long)&def_data_wr, RADIO_HCI_TIMEOUT);

	return ret;
}

int hci_fm_do_calibration(void __user *arg, struct hci_dev *hdev)
{
	__u8 __user *ptr = arg;
	int ret = 0;
	__u8 mode;

	if (copy_from_user(&mode, ptr, sizeof(mode)))
		return -EFAULT;

	ret = radio_hci_request(hdev, hci_fm_do_calibration_req, mode,
		RADIO_HCI_TIMEOUT);

	return ret;
}

int hci_read_grp_counters(void __user *arg, struct hci_dev *hdev)
{
	__u8 __user *ptr = arg;
	int ret = 0;
	__u8 reset_counters;

	if (copy_from_user(&reset_counters, ptr, sizeof(reset_counters)))
		return -EFAULT;

	ret = radio_hci_request(hdev, hci_read_grp_counters_req,
		reset_counters, RADIO_HCI_TIMEOUT);

	return ret;
}

int hci_peek_data(void __user *arg, struct hci_dev *hdev)
{
	__u8 __user *ptr = arg;
	int ret = 0;
	struct hci_fm_peek_req peek_data;

	if (copy_from_user(&peek_data, ptr, sizeof(peek_data)))
		return -EFAULT;

	ret = radio_hci_request(hdev, hci_peek_data_req, (unsigned
		long)&peek_data, RADIO_HCI_TIMEOUT);

	return ret;
}

int hci_poke_data(void __user *arg, struct hci_dev *hdev)
{
	__u8 __user *ptr = arg;
	int ret = 0;
	struct hci_fm_poke_req poke_data;

	if (copy_from_user(&poke_data, ptr, sizeof(poke_data)))
		return -EFAULT;

	ret = radio_hci_request(hdev, hci_poke_data_req, (unsigned
		long)&poke_data, RADIO_HCI_TIMEOUT);

	return ret;
}

int hci_ssbi_peek_reg(void __user *arg, struct hci_dev *hdev)
{
	__u8 __user *ptr = arg;
	int ret = 0;
	struct hci_fm_ssbi_req ssbi_peek_reg;

	if (copy_from_user(&ssbi_peek_reg, ptr, sizeof(ssbi_peek_reg)))
		return -EFAULT;

	ret = radio_hci_request(hdev, hci_ssbi_peek_reg_req, (unsigned
		long)&ssbi_peek_reg, RADIO_HCI_TIMEOUT);

	return ret;
}

int hci_ssbi_poke_reg(void __user *arg, struct hci_dev *hdev)
{
	__u8 __user *ptr = arg;
	int ret = 0;
	struct hci_fm_ssbi_req ssbi_poke_reg;

	if (copy_from_user(&ssbi_poke_reg, ptr, sizeof(ssbi_poke_reg)))
		return -EFAULT;

	ret = radio_hci_request(hdev, hci_ssbi_poke_reg_req, (unsigned
		long)&ssbi_poke_reg, RADIO_HCI_TIMEOUT);

	return ret;
}

static int hci_cmd(unsigned int cmd, struct hci_dev *hdev)
{
	int ret = 0;
	unsigned long arg = 0;

	switch (cmd) {
	case HCI_FM_ENABLE_RECV_CMD:
		ret = radio_hci_request(hdev, hci_fm_enable_recv_req, arg,
			msecs_to_jiffies(RADIO_HCI_TIMEOUT));
		break;

	case HCI_FM_DISABLE_RECV_CMD:
		ret = radio_hci_request(hdev, hci_fm_disable_recv_req, arg,
			msecs_to_jiffies(RADIO_HCI_TIMEOUT));
		break;

	case HCI_FM_GET_RECV_CONF_CMD:
		ret = radio_hci_request(hdev, hci_get_fm_recv_conf_req, arg,
			msecs_to_jiffies(RADIO_HCI_TIMEOUT));
		break;

	case HCI_FM_GET_STATION_PARAM_CMD:
		ret = radio_hci_request(hdev,
			hci_fm_get_station_param_req, arg,
			msecs_to_jiffies(RADIO_HCI_TIMEOUT));
		break;

	case HCI_FM_GET_SIGNAL_TH_CMD:
		ret = radio_hci_request(hdev,
			hci_fm_get_sig_threshold_req, arg,
			msecs_to_jiffies(RADIO_HCI_TIMEOUT));
		break;

	case HCI_FM_GET_PROGRAM_SERVICE_CMD:
		ret = radio_hci_request(hdev,
			hci_fm_get_program_service_req, arg,
			msecs_to_jiffies(RADIO_HCI_TIMEOUT));
		break;

	case HCI_FM_GET_RADIO_TEXT_CMD:
		ret = radio_hci_request(hdev, hci_fm_get_radio_text_req, arg,
			msecs_to_jiffies(RADIO_HCI_TIMEOUT));
		break;

	case HCI_FM_GET_AF_LIST_CMD:
		ret = radio_hci_request(hdev, hci_fm_get_af_list_req, arg,
			msecs_to_jiffies(RADIO_HCI_TIMEOUT));
		break;

	case HCI_FM_CANCEL_SEARCH_CMD:
		ret = radio_hci_request(hdev, hci_fm_cancel_search_req, arg,
			msecs_to_jiffies(RADIO_HCI_TIMEOUT));
		break;

	case HCI_FM_RESET_CMD:
		ret = radio_hci_request(hdev, hci_fm_reset_req, arg,
			msecs_to_jiffies(RADIO_HCI_TIMEOUT));
		break;

	case HCI_FM_GET_FEATURES_CMD:
		ret = radio_hci_request(hdev,
		hci_fm_get_feature_lists_req, arg,
			msecs_to_jiffies(RADIO_HCI_TIMEOUT));
		break;

	case HCI_FM_STATION_DBG_PARAM_CMD:
		ret = radio_hci_request(hdev,
		hci_fm_get_station_dbg_param_req, arg,
			msecs_to_jiffies(RADIO_HCI_TIMEOUT));
		break;

	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static void radio_hci_req_complete(struct hci_dev *hdev, int result)
{
	if (hdev->req_status == HCI_REQ_PEND) {
		hdev->req_result = result;
		hdev->req_status = HCI_REQ_DONE;
		wake_up_interruptible(&hdev->req_wait_q);
	}
}

static void radio_hci_status_complete(struct hci_dev *hdev, int result)
{
	if (hdev->req_status == HCI_REQ_PEND) {
		hdev->req_result = result;
		hdev->req_status = HCI_REQ_STATUS;
		wake_up_interruptible(&hdev->req_wait_q);
	}
}

static void hci_cc_rsp(struct hci_dev *hdev, struct sk_buff *skb)
{
	__u8 status = *((__u8 *) skb->data);

	if (status)
		return;

	radio_hci_req_complete(hdev, status);
}

static void hci_cc_fm_disable_rsp(struct hci_dev *hdev, struct sk_buff *skb)
{
	__u8 status = *((__u8 *) skb->data);
	struct iris_device *radio = video_get_drvdata(video_get_dev());

	if (status)
		return;

	iris_q_event(radio, IRIS_EVT_RADIO_READY);

	radio_hci_req_complete(hdev, status);
}

static void hci_cc_conf_rsp(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_fm_conf_rsp  *rsp = (void *)skb->data;

	if (rsp->status)
		return;

	radio_hci_req_complete(hdev, rsp->status);
}

static void hci_cc_fm_enable_rsp(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_fm_conf_rsp  *rsp = (void *)skb->data;

	if (rsp->status)
		return;

	radio_hci_req_complete(hdev, rsp->status);
}

static void hci_cc_sig_threshold_rsp(struct hci_dev *hdev,
		struct sk_buff *skb)
{
	struct hci_fm_sig_threshold_rsp  *rsp = (void *)skb->data;
	struct iris_device *radio = video_get_drvdata(video_get_dev());
	struct v4l2_control *v4l_ctl = radio->g_ctl;

	if (rsp->status)
		return;

	v4l_ctl->value = rsp->sig_threshold;
	radio_hci_req_complete(hdev, rsp->status);
}

static void hci_cc_station_rsp(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct iris_device *radio = video_get_drvdata(video_get_dev());
	struct hci_fm_station_rsp *rsp = (void *)skb->data;
	radio->fm_st_rsp = *(rsp);

	if (radio->fm_st_rsp.status)
		return;

	radio_hci_req_complete(hdev, radio->fm_st_rsp.status);
}

static void hci_cc_prg_srv_rsp(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_fm_prgm_srv_rsp  *rsp = (void *)skb->data;

	if (rsp->status)
		return;

	radio_hci_req_complete(hdev, rsp->status);
}

static void hci_cc_rd_txt_rsp(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_fm_radio_txt_rsp  *rsp = (void *)skb->data;

	if (rsp->status)
		return;

	radio_hci_req_complete(hdev, rsp->status);
}

static void hci_cc_af_list_rsp(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_fm_af_list_rsp  *rsp = (void *)skb->data;

	if (rsp->status)
		return;

	radio_hci_req_complete(hdev, rsp->status);
}

static void hci_cc_data_rd_rsp(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_fm_data_rd_rsp  *rsp = (void *)skb->data;

	if (rsp->status)
		return;

	radio_hci_req_complete(hdev, rsp->status);
}

static void hci_cc_feature_list_rsp(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_fm_feature_list_rsp  *rsp = (void *)skb->data;
	struct iris_device *radio = video_get_drvdata(video_get_dev());
	struct v4l2_capability *v4l_cap = radio->g_cap;

	if (rsp->status)
		return;

	v4l_cap->capabilities = (rsp->feature_mask & 0x000002) |
		(rsp->feature_mask & 0x000001);
	radio_hci_req_complete(hdev, rsp->status);
}

static void hci_cc_dbg_param_rsp(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct iris_device *radio = video_get_drvdata(video_get_dev());
	struct hci_fm_dbg_param_rsp *rsp = (void *)skb->data;
	radio->st_dbg_param = *(rsp);

	if (radio->st_dbg_param.status)
		return;

	radio_hci_req_complete(hdev, radio->st_dbg_param.status);
}

static inline void hci_cmd_complete_event(struct hci_dev *hdev,
		struct sk_buff *skb)
{
	struct hci_ev_cmd_complete *cmd_compl_ev = (void *) skb->data;
	__u16 opcode;

	skb_pull(skb, sizeof(*cmd_compl_ev));

	opcode = __le16_to_cpu(cmd_compl_ev->cmd_opcode);

	switch (opcode) {
	case hci_recv_ctrl_cmd_op_pack(HCI_OCF_FM_ENABLE_RECV_REQ):
		hci_cc_fm_enable_rsp(hdev, skb);
		break;
	case hci_recv_ctrl_cmd_op_pack(HCI_OCF_FM_GET_RECV_CONF_REQ):
		hci_cc_conf_rsp(hdev, skb);
		break;

	case hci_recv_ctrl_cmd_op_pack(HCI_OCF_FM_DISABLE_RECV_REQ):
		hci_cc_fm_disable_rsp(hdev, skb);
		break;

	case hci_recv_ctrl_cmd_op_pack(HCI_OCF_FM_SET_RECV_CONF_REQ):
	case hci_recv_ctrl_cmd_op_pack(HCI_OCF_FM_SET_MUTE_MODE_REQ):
	case hci_recv_ctrl_cmd_op_pack(HCI_OCF_FM_SET_STEREO_MODE_REQ):
	case hci_recv_ctrl_cmd_op_pack(HCI_OCF_FM_SET_ANTENNA):
	case hci_recv_ctrl_cmd_op_pack(HCI_OCF_FM_SET_SIGNAL_THRESHOLD):
	case hci_recv_ctrl_cmd_op_pack(HCI_OCF_FM_CANCEL_SEARCH):
	case hci_recv_ctrl_cmd_op_pack(HCI_OCF_FM_RDS_GRP):
	case hci_recv_ctrl_cmd_op_pack(HCI_OCF_FM_RDS_GRP_PROCESS):
	case hci_recv_ctrl_cmd_op_pack(HCI_OCF_FM_EN_WAN_AVD_CTRL):
	case hci_recv_ctrl_cmd_op_pack(HCI_OCF_FM_EN_NOTCH_CTRL):
	case hci_common_cmd_op_pack(HCI_OCF_FM_DEFAULT_DATA_WRITE):
	case hci_common_cmd_op_pack(HCI_OCF_FM_RESET):
	case hci_status_param_op_pack(HCI_OCF_FM_READ_GRP_COUNTERS):
	case hci_diagnostic_cmd_op_pack(HCI_OCF_FM_POKE_DATA):
	case hci_diagnostic_cmd_op_pack(HCI_OCF_FM_SSBI_PEEK_REG):
	case hci_diagnostic_cmd_op_pack(HCI_OCF_FM_SSBI_POKE_REG):
		hci_cc_rsp(hdev, skb);
		break;

	case hci_recv_ctrl_cmd_op_pack(HCI_OCF_FM_GET_SIGNAL_THRESHOLD):
		hci_cc_sig_threshold_rsp(hdev, skb);
		break;

	case hci_recv_ctrl_cmd_op_pack(HCI_OCF_FM_GET_STATION_PARAM_REQ):
		hci_cc_station_rsp(hdev, skb);
		break;

	case hci_recv_ctrl_cmd_op_pack(HCI_OCF_FM_GET_PROGRAM_SERVICE_REQ):
		hci_cc_prg_srv_rsp(hdev, skb);
		break;

	case hci_recv_ctrl_cmd_op_pack(HCI_OCF_FM_GET_RADIO_TEXT_REQ):
		hci_cc_rd_txt_rsp(hdev, skb);
		break;

	case hci_recv_ctrl_cmd_op_pack(HCI_OCF_FM_GET_AF_LIST_REQ):
		hci_cc_af_list_rsp(hdev, skb);
		break;

	case hci_common_cmd_op_pack(HCI_OCF_FM_DEFAULT_DATA_READ):
	case hci_diagnostic_cmd_op_pack(HCI_OCF_FM_PEEK_DATA):
		hci_cc_data_rd_rsp(hdev, skb);
		break;

	case hci_common_cmd_op_pack(HCI_OCF_FM_GET_FEATURE_LIST):
		hci_cc_feature_list_rsp(hdev, skb);
		break;

	case hci_diagnostic_cmd_op_pack(HCI_OCF_FM_STATION_DBG_PARAM):
		hci_cc_dbg_param_rsp(hdev, skb);
		break;

	default:
		FMDERR("%s opcode 0x%x", hdev->name, opcode);
		break;
	}
	if (cmd_compl_ev->num_hci_cmd_pkts) {
		atomic_set(&hdev->cmd_cnt, 1);
		if (!skb_queue_empty(&hdev->cmd_q))
			tasklet_schedule(&hdev->cmd_task);
	}
}

static inline void hci_cmd_status_event(struct hci_dev *hdev,
		struct sk_buff *skb)
{
	struct hci_ev_cmd_status *ev = (void *) skb->data;
	radio_hci_status_complete(hdev, ev->status);

	if (ev->num_hci_cmd_pkts) {
		atomic_set(&hdev->cmd_cnt, 1);
		if (!skb_queue_empty(&hdev->cmd_q))
			tasklet_schedule(&hdev->cmd_task);
	}
}

static inline void hci_ev_tune_status(struct hci_dev *hdev,
		struct sk_buff *skb)
{
	struct hci_ev_tune_status *ev = (void *) skb->data;
	struct iris_device *radio = video_get_drvdata(video_get_dev());
	radio->fm_st_rsp.station_rsp = *ev;
}

static inline void hci_ev_tune_compl(struct hci_dev *hdev,
		struct sk_buff *skb)
{
	int i;
	__u8 status = *((__u8 *) skb->data);
	struct iris_device *radio = video_get_drvdata(video_get_dev());

	if (!status) {
		iris_q_event(radio, IRIS_EVT_TUNE_SUCC);
		for (i = 0; i < IRIS_BUF_MAX; i++) {
			if (i >= IRIS_BUF_RT_RDS)
				kfifo_reset(&radio->data_buf[i]);
		}
		if (radio->fm_st_rsp.station_rsp.rssi)
			iris_q_event(radio, IRIS_EVT_ABOVE_TH);
		else
			iris_q_event(radio, IRIS_EVT_BELOW_TH);
		if (radio->fm_st_rsp.station_rsp.stereo_prg)
			iris_q_event(radio, IRIS_EVT_STEREO);
		if (radio->fm_st_rsp.station_rsp.mute_mode)
			iris_q_event(radio, IRIS_EVT_MONO);
		if (radio->fm_st_rsp.station_rsp.rds_sync_status)
			iris_q_event(radio, IRIS_EVT_RDS_AVAIL);
		else
			iris_q_event(radio, IRIS_EVT_RDS_NOT_AVAIL);
	}
}

static inline void hci_ev_search_compl(struct hci_dev *hdev,
		struct sk_buff *skb)
{
	struct iris_device *radio = video_get_drvdata(video_get_dev());
	iris_q_event(radio, IRIS_EVT_SEEK_COMPLETE);
}

static inline void hci_ev_srch_st_list_compl(struct hci_dev *hdev,
		struct sk_buff *skb)
{
	struct iris_device *radio = video_get_drvdata(video_get_dev());
	struct hci_ev_srch_list_compl *ev = (void *) skb->data;
	radio->srch_st_result = *ev;
}

static inline void hci_ev_search_next(struct hci_dev *hdev,
		struct sk_buff *skb)
{
	struct iris_device *radio = video_get_drvdata(video_get_dev());
	iris_q_event(radio, IRIS_EVT_SCAN_NEXT);
}

static inline void hci_ev_stereo_status(struct hci_dev *hdev,
		struct sk_buff *skb)
{
	struct iris_device *radio = video_get_drvdata(video_get_dev());
	__u8 st_status = *((__u8 *) skb->data);
	if (st_status)
		iris_q_event(radio, IRIS_EVT_STEREO);
	else
		iris_q_event(radio, IRIS_EVT_MONO);
}

void radio_hci_event_packet(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct radio_hci_event_hdr *hdr = (void *) skb->data;
	__u8 event = hdr->evt;

	skb_pull(skb, RADIO_HCI_EVENT_HDR_SIZE);

	switch (event) {
	case HCI_EV_TUNE_STATUS:
		hci_ev_tune_status(hdev, skb);
		break;
	case HCI_EV_SEARCH_PROGRESS:
	case HCI_EV_SEARCH_RDS_PROGRESS:
	case HCI_EV_SEARCH_LIST_PROGRESS:
		hci_ev_search_next(hdev, skb);
		break;
	case HCI_EV_STEREO_STATUS:
		hci_ev_stereo_status(hdev, skb);
		break;
	case HCI_EV_RDS_LOCK_STATUS:
	case HCI_EV_SERVICE_AVAILABLE:
	case HCI_EV_RDS_RX_DATA:
	case HCI_EV_PROGRAM_SERVICE:
	case HCI_EV_RADIO_TEXT:
	case HCI_EV_FM_AF_LIST:
	case HCI_EV_TX_RDS_GRP_COMPL:
	case HCI_EV_TX_RDS_CONT_GRP_COMPL:
		break;

	case HCI_EV_CMD_COMPLETE:
		hci_cmd_complete_event(hdev, skb);
		break;

	case HCI_EV_CMD_STATUS:
		hci_cmd_status_event(hdev, skb);
		break;

	case HCI_EV_TUNE_COMPLETE:
		hci_ev_tune_compl(hdev, skb);
		break;

	case HCI_EV_SEARCH_COMPLETE:
	case HCI_EV_SEARCH_RDS_COMPLETE:
		hci_ev_search_compl(hdev, skb);
		break;

	case HCI_EV_SEARCH_LIST_COMPLETE:
		break;

	default:
		break;
	}
	kfree_skb(skb);
}

/*
 * fops/IOCTL helper functions
 */

static int iris_search(struct iris_device *radio, int on, int dir)
{
	int retval = 0;
	enum search_t srch = radio->g_search_mode & SRCH_MODE;

	if (on) {
		switch (srch) {
		case SCAN_FOR_STRONG:
		case SCAN_FOR_WEAK:
			radio->srch_st_list.srch_list_dir = dir;
			retval = hci_fm_search_station_list(
				&radio->srch_st_list, radio->fm_hdev);
			break;
		case RDS_SEEK_PTY:
		case RDS_SCAN_PTY:
		case RDS_SEEK_PI:
			radio->srch_rds.srch_station.srch_dir = dir;
			radio->srch_rds.srch_station.scan_time =
				radio->g_scan_time;
			retval = hci_fm_search_rds_stations(&radio->srch_rds,
				radio->fm_hdev);
			break;
		default:
			radio->srch_st.scan_time = radio->g_scan_time;
			radio->srch_st.srch_dir = dir;
			retval = hci_fm_search_stations(
				&radio->srch_st, radio->fm_hdev);
			break;
		}

	} else {
		FMDERR("Search ctrl->value is 0\n");
		retval = hci_cmd(HCI_FM_CANCEL_SEARCH_CMD, radio->fm_hdev);
	}

	return retval;
}

static int iris_set_region(struct iris_device *radio, int req_region)
{
	int retval;
	radio->region = req_region;

	switch (radio->region) {
	case IRIS_REGION_US:
		{
			radio->recv_conf.band_low_limit = 88100;
			radio->recv_conf.band_high_limit = 108000;
			radio->recv_conf.emphasis = 0;
			radio->recv_conf.hlsi = 0;
			radio->recv_conf.ch_spacing = 0;
			radio->recv_conf.rds_std = 0;
		}
		break;
	case IRIS_REGION_EU:
		{
			radio->recv_conf.band_low_limit = 88100;
			radio->recv_conf.band_high_limit = 108000;
			radio->recv_conf.emphasis = 0;
			radio->recv_conf.hlsi = 0;
			radio->recv_conf.ch_spacing = 1;
			radio->recv_conf.rds_std = 1;
		}
		break;
	case IRIS_REGION_JAPAN:
		{
			radio->recv_conf.band_low_limit = 76000;
			radio->recv_conf.band_high_limit = 108000;
			radio->recv_conf.emphasis = 1;
			radio->recv_conf.hlsi = 0;
			radio->recv_conf.ch_spacing = 1;
		}
		break;
	default:
		{
			radio->recv_conf.emphasis = 0;
			radio->recv_conf.hlsi = 0;
			radio->recv_conf.ch_spacing = 0;
			radio->recv_conf.rds_std = 2;
		}
		break;
	}


	retval = hci_set_fm_recv_conf(
			&radio->recv_conf,
			radio->fm_hdev);
	if (retval < 0) {
		FMDERR("Could not set recv configuration\n");
		return retval;
	}
	return retval;
}

static int iris_set_freq(struct iris_device *radio, unsigned int freq)
{

	int retval;
	retval = hci_fm_tune_station(&freq, radio->fm_hdev);
	if (retval < 0)
		FMDERR("Error while setting the frequency : %d\n", retval);
	return retval;
}

static int iris_vidioc_querycap(struct file *file, void *priv,
		struct v4l2_capability *capability)
{
	int retval;
	struct iris_device *radio = video_get_drvdata(video_devdata(file));

	strlcpy(capability->driver, DRIVER_NAME, sizeof(capability->driver));
	strlcpy(capability->card, DRIVER_CARD, sizeof(capability->card));
	radio->g_cap = capability;
	retval = hci_cmd(HCI_FM_GET_FEATURES_CMD, radio->fm_hdev);
	if (retval < 0)
		FMDERR("videoc querycap failed with %d\n", retval);
	return retval;
}

static int iris_vidioc_queryctrl(struct file *file, void *priv,
		struct v4l2_queryctrl *qc)
{
	unsigned char i;
	int retval = -EINVAL;

	for (i = 0; i < ARRAY_SIZE(iris_v4l2_queryctrl); i++) {
		if (qc->id && qc->id == iris_v4l2_queryctrl[i].id) {
			memcpy(qc, &(iris_v4l2_queryctrl[i]), sizeof(*qc));
			retval = 0;
			break;
		}
	}
	if (retval < 0)
		FMDERR(": query conv4ltrol failed with %d\n", retval);

	return retval;
}

static int iris_vidioc_g_ctrl(struct file *file, void *priv,
		struct v4l2_control *ctrl)
{
	struct iris_device *radio = video_get_drvdata(video_devdata(file));
	int retval = 0;

	switch (ctrl->id) {
	case V4L2_CID_AUDIO_VOLUME:
		break;
	case V4L2_CID_AUDIO_MUTE:
		ctrl->value = radio->mute_mode.hard_mute;
		break;
	case V4L2_CID_PRIVATE_IRIS_SRCHMODE:
		ctrl->value = radio->g_search_mode;
		break;
	case V4L2_CID_PRIVATE_IRIS_SCANDWELL:
		ctrl->value = radio->g_scan_time;
		break;
	case V4L2_CID_PRIVATE_IRIS_SRCHON:
		break;
	case V4L2_CID_PRIVATE_IRIS_STATE:
		break;
	case V4L2_CID_PRIVATE_IRIS_IOVERC:
		retval = hci_cmd(HCI_FM_STATION_DBG_PARAM_CMD, radio->fm_hdev);
		if (retval < 0) {
			FMDERR("Error while getting station dbg param: %d\n",
			retval);
			return retval;
		}
		ctrl->value = radio->st_dbg_param.io_verc;
		break;
	case V4L2_CID_PRIVATE_IRIS_INTDET:
		retval = hci_cmd(HCI_FM_STATION_DBG_PARAM_CMD, radio->fm_hdev);
		if (retval < 0) {
			FMDERR("Error while getting station dbg param: %d\n",
			retval);
			return retval;
		}
		ctrl->value = radio->st_dbg_param.in_det_out;
		break;
	case V4L2_CID_PRIVATE_IRIS_REGION:
		ctrl->value = radio->region;
		break;
	case V4L2_CID_PRIVATE_IRIS_SIGNAL_TH:
		retval = hci_cmd(HCI_FM_GET_SIGNAL_TH_CMD, radio->fm_hdev);
		break;
	case V4L2_CID_PRIVATE_IRIS_SRCH_PTY:
		break;
	case V4L2_CID_PRIVATE_IRIS_SRCH_PI:
		break;
	case V4L2_CID_PRIVATE_IRIS_SRCH_CNT:
		break;
	case V4L2_CID_PRIVATE_IRIS_EMPHASIS:
		ctrl->value = radio->recv_conf.emphasis;
		break;
	case V4L2_CID_PRIVATE_IRIS_RDS_STD:
		ctrl->value = radio->recv_conf.rds_std;
		break;
	case V4L2_CID_PRIVATE_IRIS_SPACING:
		ctrl->value = radio->recv_conf.ch_spacing;
		break;
	case V4L2_CID_PRIVATE_IRIS_RDSON:
		ctrl->value = radio->recv_conf.rds_std;
		break;
	case V4L2_CID_PRIVATE_IRIS_RDSGROUP_MASK:
		ctrl->value = radio->rds_grp.rds_grp_enable_mask;
		break;
	case V4L2_CID_PRIVATE_IRIS_RDSGROUP_PROC:
		break;
	case V4L2_CID_PRIVATE_IRIS_RDSD_BUF:
		ctrl->value = radio->rds_grp.rds_buf_size;
		break;
	case V4L2_CID_PRIVATE_IRIS_PSALL:
		ctrl->value = radio->g_rds_grp_proc_ps;
		break;
	case V4L2_CID_PRIVATE_IRIS_LP_MODE:
		break;
	case V4L2_CID_PRIVATE_IRIS_ANTENNA:
		ctrl->value = radio->g_antenna;
		break;
	default:
		retval = -EINVAL;
	}
	if (retval < 0)
		FMDERR("get control failed with %d, id: %d\n",
			retval, ctrl->id);
	return retval;
}

static int iris_vidioc_s_ext_ctrls(struct file *file, void *priv,
			struct v4l2_ext_controls *ctrl)
{
	return -ENOTSUPP;
}

static int iris_vidioc_s_ctrl(struct file *file, void *priv,
		struct v4l2_control *ctrl)
{
	struct iris_device *radio = video_get_drvdata(video_devdata(file));
	int retval = 0;
	unsigned int rds_grps_proc = 0;

	switch (ctrl->id) {
	case V4L2_CID_AUDIO_VOLUME:
		break;
	case V4L2_CID_AUDIO_MUTE:
		radio->mute_mode.hard_mute = ctrl->value;
		radio->mute_mode.soft_mute = IOC_SFT_MUTE;
		retval = hci_set_fm_mute_mode(
				&radio->mute_mode,
				radio->fm_hdev);
		if (retval < 0) {
				FMDERR("Error while set FM hard mute"" %d\n",
				retval);
		}
		break;
	case V4L2_CID_PRIVATE_IRIS_SRCHMODE:
		radio->g_search_mode = ctrl->value;
		break;
	case V4L2_CID_PRIVATE_IRIS_SCANDWELL:
		radio->g_scan_time = ctrl->value;
		break;
	case V4L2_CID_PRIVATE_IRIS_SRCHON:
		iris_search(radio, ctrl->value, SRCH_DIR_UP);
		break;
	case V4L2_CID_PRIVATE_IRIS_STATE:
		if (ctrl->value == FM_RECV) {
			retval = hci_cmd(HCI_FM_ENABLE_RECV_CMD,
							 radio->fm_hdev);
			if (retval < 0) {
				FMDERR("Error while enabling FM"
							" %d\n", retval);
			} else {
				radio->mute_mode.soft_mute = 0x01;
				retval = hci_set_fm_mute_mode(
							&radio->mute_mode,
							radio->fm_hdev);
				if (retval < 0)
					FMDERR("SMute not enabled\n");
				radio->stereo_mode.stereo_mode = 0x01;
				radio->stereo_mode.sig_blend = 0x01;
				retval = hci_set_fm_stereo_mode(
							&radio->stereo_mode,
							radio->fm_hdev);
				if (retval < 0)
					FMDERR("Signal Blending is "
							   "not enabled\n");
			}
		} else if (ctrl->value == FM_OFF) {
			retval = hci_cmd(
							HCI_FM_DISABLE_RECV_CMD,
							radio->fm_hdev);
			if (retval < 0) {
				FMDERR("Error on disable FM"
						" %d\n", retval);
			}
		}
		break;
	case V4L2_CID_PRIVATE_IRIS_REGION:
		retval = iris_set_region(radio, ctrl->value);
		break;
	case V4L2_CID_PRIVATE_IRIS_SIGNAL_TH:
		retval = hci_fm_set_signal_threshold(
				&ctrl->value,
				radio->fm_hdev);
		if (retval < 0) {
			FMDERR("error while setting signal threshold\n");
			break;
		}
		break;
	case V4L2_CID_PRIVATE_IRIS_SRCH_PTY:
		radio->srch_rds.srch_pty = ctrl->value;
		radio->srch_st_list.srch_pty = ctrl->value;
		break;
	case V4L2_CID_PRIVATE_IRIS_SRCH_PI:
		radio->srch_rds.srch_pi = ctrl->value;
		break;
	case V4L2_CID_PRIVATE_IRIS_SRCH_CNT:
		break;
	case V4L2_CID_PRIVATE_IRIS_SPACING:
		radio->recv_conf.ch_spacing = ctrl->value;
		break;
	case V4L2_CID_PRIVATE_IRIS_EMPHASIS:
		radio->recv_conf.emphasis = ctrl->value;
		retval =
		hci_set_fm_recv_conf(&radio->recv_conf, radio->fm_hdev);
		break;
	case V4L2_CID_PRIVATE_IRIS_RDS_STD:
		radio->recv_conf.rds_std = ctrl->value;
		retval =
		hci_set_fm_recv_conf(&radio->recv_conf, radio->fm_hdev);
		break;
	case V4L2_CID_PRIVATE_IRIS_RDSON:
		radio->recv_conf.rds_std = ctrl->value;
		retval =
		hci_set_fm_recv_conf(&radio->recv_conf, radio->fm_hdev);
		break;
	case V4L2_CID_PRIVATE_IRIS_RDSGROUP_MASK:
		radio->rds_grp.rds_grp_enable_mask = ctrl->value;
		retval = hci_fm_rds_grp(&radio->rds_grp, radio->fm_hdev);
		break;
	case V4L2_CID_PRIVATE_IRIS_RDSGROUP_PROC:
		rds_grps_proc = radio->g_rds_grp_proc_ps | ctrl->value;
		retval = hci_fm_rds_grps_process(
				&rds_grps_proc,
				radio->fm_hdev);
		break;
	case V4L2_CID_PRIVATE_IRIS_RDSD_BUF:
		radio->rds_grp.rds_buf_size = ctrl->value;
		break;
	case V4L2_CID_PRIVATE_IRIS_PSALL:
		radio->g_rds_grp_proc_ps = ctrl->value;
		break;
	case V4L2_CID_PRIVATE_IRIS_LP_MODE:
		break;
	case V4L2_CID_PRIVATE_IRIS_ANTENNA:
		radio->g_antenna = ctrl->value;
		retval = hci_fm_set_antenna(&radio->g_antenna, radio->fm_hdev);
		break;
	case V4L2_CID_RDS_TX_PTY:
		break;
	case V4L2_CID_RDS_TX_PI:
		break;
	case V4L2_CID_PRIVATE_IRIS_STOP_RDS_TX_PS_NAME:
		break;
	case V4L2_CID_PRIVATE_IRIS_STOP_RDS_TX_RT:
		break;
	case V4L2_CID_PRIVATE_IRIS_TX_SETPSREPEATCOUNT:
		break;
	case V4L2_CID_TUNE_POWER_LEVEL:
		break;
	default:
		retval = -EINVAL;
	}
	return retval;
}

static int iris_vidioc_g_tuner(struct file *file, void *priv,
		struct v4l2_tuner *tuner)
{
	struct iris_device *radio = video_get_drvdata(video_devdata(file));
	int retval;
	if (tuner->index > 0)
		return -EINVAL;

	retval = hci_cmd(HCI_FM_GET_STATION_PARAM_CMD, radio->fm_hdev);
	if (retval < 0)
		return retval;

	tuner->type = V4L2_TUNER_RADIO;
	tuner->rangelow  = radio->recv_conf.band_low_limit;
	tuner->rangehigh = radio->recv_conf.band_high_limit;
	tuner->rxsubchans = V4L2_TUNER_SUB_MONO | V4L2_TUNER_SUB_STEREO;
	tuner->capability = V4L2_TUNER_CAP_LOW;
	tuner->signal = radio->fm_st_rsp.station_rsp.rssi;
	tuner->audmode = radio->fm_st_rsp.station_rsp.stereo_prg;
	tuner->afc = 0;

	return 0;
}

static int iris_vidioc_s_tuner(struct file *file, void *priv,
		struct v4l2_tuner *tuner)
{
	struct iris_device *radio = video_get_drvdata(video_devdata(file));
	int retval;
	if (tuner->index > 0)
		return -EINVAL;

	radio->recv_conf.band_low_limit = tuner->rangelow;
	radio->recv_conf.band_high_limit = tuner->rangehigh;
	if (tuner->audmode == V4L2_TUNER_MODE_MONO) {
		radio->stereo_mode.stereo_mode = 0x01;
		retval = hci_set_fm_stereo_mode(
				&radio->stereo_mode,
				radio->fm_hdev);
	} else {
		radio->stereo_mode.stereo_mode = 0x00;
		retval = hci_set_fm_stereo_mode(
				&radio->stereo_mode,
				radio->fm_hdev);
	}
	if (retval < 0)
		FMDERR(": set tuner failed with %d\n", retval);
	return retval;
}

static int iris_vidioc_g_frequency(struct file *file, void *priv,
		struct v4l2_frequency *freq)
{
	struct iris_device *radio = video_get_drvdata(video_devdata(file));
	int retval;

	freq->type = V4L2_TUNER_RADIO;
	retval = hci_cmd(HCI_FM_GET_STATION_PARAM_CMD, radio->fm_hdev);
	if (retval < 0)
		FMDERR("get frequency failed %d\n", retval);
	else
		freq->frequency = radio->fm_st_rsp.station_rsp.station_freq;
	return retval;
}

static int iris_vidioc_s_frequency(struct file *file, void *priv,
					struct v4l2_frequency *freq)
{
	struct iris_device  *radio = video_get_drvdata(video_devdata(file));
	int retval = -1;

	if (freq->type != V4L2_TUNER_RADIO)
		return -EINVAL;

	retval = iris_set_freq(radio, freq->frequency);
	if (retval < 0)
		FMDERR(" set frequency failed with %d\n", retval);
	return retval;
}

static int iris_vidioc_dqbuf(struct file *file, void *priv,
				struct v4l2_buffer *buffer)
{
	struct iris_device  *radio = video_get_drvdata(video_devdata(file));
	enum iris_buf_t buf_type = buffer->index;
	struct kfifo *data_fifo;
	unsigned char *buf = (unsigned char *)buffer->m.userptr;
	unsigned int len = buffer->length;
	if (!access_ok(VERIFY_WRITE, buf, len))
		return -EFAULT;
	if ((buf_type < IRIS_BUF_MAX) && (buf_type >= 0)) {
		data_fifo = &radio->data_buf[buf_type];
		if (buf_type == IRIS_BUF_EVENTS) {
			if (wait_event_interruptible(radio->event_queue,
				kfifo_len(data_fifo)) < 0) {
				return -EINTR;
			}
		}
	} else {
		FMDERR("invalid buffer type\n");
		return -EINVAL;
	}
	buffer->bytesused = kfifo_out_locked(data_fifo, buf, len,
					&radio->buf_lock[buf_type]);

	return 0;
}

static int iris_vidioc_s_hw_freq_seek(struct file *file, void *priv,
					struct v4l2_hw_freq_seek *seek)
{
	struct iris_device *radio = video_get_drvdata(video_devdata(file));
	int dir;
	if (seek->seek_upward)
		dir = SRCH_DIR_UP;
	else
		dir = SRCH_DIR_DOWN;
	return iris_search(radio, CTRL_ON, dir);
}

static const struct v4l2_ioctl_ops iris_ioctl_ops = {
	.vidioc_querycap              = iris_vidioc_querycap,
	.vidioc_queryctrl             = iris_vidioc_queryctrl,
	.vidioc_g_ctrl                = iris_vidioc_g_ctrl,
	.vidioc_s_ctrl                = iris_vidioc_s_ctrl,
	.vidioc_g_tuner               = iris_vidioc_g_tuner,
	.vidioc_s_tuner               = iris_vidioc_s_tuner,
	.vidioc_g_frequency           = iris_vidioc_g_frequency,
	.vidioc_s_frequency           = iris_vidioc_s_frequency,
	.vidioc_s_hw_freq_seek        = iris_vidioc_s_hw_freq_seek,
	.vidioc_dqbuf                 = iris_vidioc_dqbuf,
	.vidioc_s_ext_ctrls           = iris_vidioc_s_ext_ctrls,
};

static struct video_device iris_viddev_template = {
	.ioctl_ops              = &iris_ioctl_ops,
	.name                   = DRIVER_NAME,
	.release                = video_device_release,
};

static struct video_device *video_get_dev(void)
{
	return &iris_viddev_template;
}

static int  __init iris_probe(struct platform_device *pdev)
{
	struct iris_device *radio;
	int retval;
	int radio_nr = -1;
	int i;

	radio = kzalloc(sizeof(struct iris_device), GFP_KERNEL);
	if (!radio) {
		FMDERR(": Could not allocate radio device\n");
		return -ENOMEM;
	}

	radio->dev = &pdev->dev;
	platform_set_drvdata(pdev, radio);

	radio->videodev = video_device_alloc();
	if (!radio->videodev) {
		FMDERR(": Could not allocate V4L device\n");
		kfree(radio);
		return -ENOMEM;
	}

	memcpy(radio->videodev, &iris_viddev_template,
	  sizeof(iris_viddev_template));

	for (i = 0; i < IRIS_BUF_MAX; i++) {
		int kfifo_alloc_rc = 0;
		spin_lock_init(&radio->buf_lock[i]);

		if (i == IRIS_BUF_RAW_RDS)
			kfifo_alloc_rc = kfifo_alloc(&radio->data_buf[i],
				rds_buf*3, GFP_KERNEL);
		else
			kfifo_alloc_rc = kfifo_alloc(&radio->data_buf[i],
				STD_BUF_SIZE, GFP_KERNEL);

		if (kfifo_alloc_rc != 0) {
			FMDERR("failed allocating buffers %d\n",
				   kfifo_alloc_rc);
			for (; i > -1; i--) {
				kfifo_free(&radio->data_buf[i]);
				kfree(radio);
				return -ENOMEM;
			}
		}
	}

	radio->xfr_in_progress = 0;
	radio->xfr_bytes_left = 0;
	for (i = 0; i < IRIS_XFR_MAX; i++)
		radio->pending_xfrs[i] = 0;

	mutex_init(&radio->lock);
	init_completion(&radio->sync_xfr_start);
	radio->tune_req = 0;
	init_waitqueue_head(&radio->event_queue);
	init_waitqueue_head(&radio->read_queue);

	video_set_drvdata(radio->videodev, radio);
	retval = video_register_device(radio->videodev, VFL_TYPE_RADIO,
								   radio_nr);
	if (retval) {
		FMDERR(": Could not register video device\n");
		video_device_release(radio->videodev);
		for (; i > -1; i--)
			kfifo_free(&radio->data_buf[i]);
		kfree(radio);
		return retval;
	}
	return 0;
}

static int __devexit iris_remove(struct platform_device *pdev)
{
	int i;
	struct iris_device *radio = platform_get_drvdata(pdev);

	video_unregister_device(radio->videodev);

	for (i = 0; i < IRIS_BUF_MAX; i++)
		kfifo_free(&radio->data_buf[i]);

	kfree(radio);

	platform_set_drvdata(pdev, NULL);

	return 0;
}

static struct platform_driver iris_driver = {
	.driver = {
		.owner  = THIS_MODULE,
		.name   = "iris_fm",
	},
	.remove = __devexit_p(iris_remove),
};

static int __init iris_radio_init(void)
{
	return platform_driver_probe(&iris_driver, iris_probe);
}
module_init(iris_radio_init);

static void __exit iris_radio_exit(void)
{
	platform_driver_unregister(&iris_driver);
}
module_exit(iris_radio_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
