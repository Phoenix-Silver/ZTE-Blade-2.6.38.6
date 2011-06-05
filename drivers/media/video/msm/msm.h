/* Copyright (c) 2011, Code Aurora Forum. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 *     * Neither the name of Code Aurora Forum, Inc. nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#ifndef _MSM_H
#define _MSM_H

#ifdef __KERNEL__

/* Header files */
#include <linux/i2c.h>
#include <linux/videodev2.h>
#include <media/v4l2-dev.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fh.h>
#include <media/v4l2-event.h>
#include <media/v4l2-mediabus.h>
#include <media/videobuf-dma-contig.h>
#include <media/videobuf-msm-mem.h>
#include <mach/camera.h>

#define MSM_V4L2_DIMENSION_SIZE 24

#define ERR_USER_COPY(to) pr_err("%s(%d): copy %s user\n", \
				__func__, __LINE__, ((to) ? "to" : "from"))
#define ERR_COPY_FROM_USER() ERR_USER_COPY(0)
#define ERR_COPY_TO_USER() ERR_USER_COPY(1)

/* msm queue management APIs*/

#define msm_dequeue(queue, member) ({	   \
	unsigned long flags;		  \
	struct msm_device_queue *__q = (queue);	 \
	struct msm_queue_cmd *qcmd = 0;	   \
	spin_lock_irqsave(&__q->lock, flags);	 \
	if (!list_empty(&__q->list)) {		\
		__q->len--;		 \
		qcmd = list_first_entry(&__q->list,   \
		struct msm_queue_cmd, member);  \
		list_del_init(&qcmd->member);	 \
	}			 \
	spin_unlock_irqrestore(&__q->lock, flags);  \
	qcmd;			 \
})

#define msm_queue_drain(queue, member) do {	 \
	unsigned long flags;		  \
	struct msm_device_queue *__q = (queue);	 \
	struct msm_queue_cmd *qcmd;	   \
	spin_lock_irqsave(&__q->lock, flags);	 \
	while (!list_empty(&__q->list)) {	 \
		qcmd = list_first_entry(&__q->list,   \
			struct msm_queue_cmd, member);	\
			list_del_init(&qcmd->member);	 \
			free_qcmd(qcmd);		\
	 };			  \
	spin_unlock_irqrestore(&__q->lock, flags);	\
} while (0)

static inline void free_qcmd(struct msm_queue_cmd *qcmd)
{
	if (!qcmd || !atomic_read(&qcmd->on_heap))
		return;
	if (!atomic_sub_return(1, &qcmd->on_heap))
		kfree(qcmd);
}

/* message id for v4l2_subdev_notify*/
enum msm_camera_v4l2_subdev_notify {
	NOTIFY_CID_CHANGE, /* arg = msm_camera_csid_params */
	NOTIFY_VFE_MSG_EVT, /* arg = msm_vfe_resp */
	NOTIFY_INVALID
};

enum isp_vfe_cmd_id {
	/*
	*Important! Command_ID are arranged in order.
	*Don't change!*/
	ISP_VFE_CMD_ID_STREAM_ON,
	ISP_VFE_CMD_ID_STREAM_OFF,
	ISP_VFE_CMD_ID_FRAME_BUF_RELEASE
};

struct msm_cam_v4l2_device;
struct msm_cam_v4l2_dev_inst;

/* buffer for one video frame */
struct msm_frame_buffer {
	/* common v4l buffer stuff -- must be first */
	struct videobuf_buffer    vidbuf;
	enum v4l2_mbus_pixelcode  pxlcode;
	int                       inuse;
	int                       active;
};

struct msm_isp_color_fmt {
	char *name;
	int depth;
	int bitsperpxl;
	u32 fourcc;
	enum v4l2_mbus_pixelcode pxlcode;
	enum v4l2_colorspace colorspace;
};

enum ispif_op_id {
	/*
	*Important! Command_ID are arranged in order.
	*Don't change!*/
	ISPIF_ENABLE,
	ISPIF_DISABLE,
	ISPIF_RESET,
	ISPIF_CONFIG
};

struct msm_ispif_ops {

	int (*ispif_op)(struct msm_ispif_ops *p_ispif,
		enum ispif_op_id ispif_op_id_used, unsigned long arg);
};

struct msm_ispif_fns {
	int (*ispif_config)(struct msm_ispif_params *ispif_params,
						 uint8_t num_of_intf);
	int (*ispif_start_intf_transfer)
		(struct msm_ispif_params *ispif_params);
};

extern int msm_ispif_init_module(struct msm_ispif_ops *p_ispif);

/*"Media Controller" represents a camera steaming session, which consists
   of a "sensor" device and an "isp" device (such as VFE, if needed),
   connected via an "IO" device, (such as IPIF on 8960, or none on 8660)
   plus other extra sub devices such as VPE and flash
*/

struct msm_cam_media_controller {

	int (*mctl_open)(struct msm_cam_media_controller *p_mctl,
					 const char *const apps_id);
	int (*mctl_cb)(void);
	int (*mctl_notify)(struct msm_cam_media_controller *p_mctl,
			unsigned int notification, void *arg);
	int (*mctl_cmd)(struct msm_cam_media_controller *p_mctl,
					unsigned int cmd, unsigned long arg);
	int (*mctl_release)(struct msm_cam_media_controller *p_mctl);
	int (*mctl_vidbuf_init)(struct msm_cam_v4l2_dev_inst *pcam,
						struct videobuf_queue *);
	int (*mctl_ufmt_init)(struct msm_cam_media_controller *p_mctl);

	struct v4l2_device v4l2_dev;
	struct v4l2_fh  eventHandle; /* event queue to export events */
	/* most-frequently accessed manager object*/
	struct msm_sync sync;


	/* the following reflect the HW topology information*/
	/*mandatory*/
	struct v4l2_subdev *sensor_sdev; /* sensor sub device */
	struct v4l2_subdev mctl_sdev;   /*  media control sub device */
	/*optional*/
	struct msm_isp_ops *isp_sdev;    /* isp sub device : camif/VFE */
	struct v4l2_subdev *vpe_sdev;    /* vpe sub device : VPE */
	struct v4l2_subdev *flash_sdev;    /* vpe sub device : VPE */
	struct msm_cam_config_dev *config_device;
	struct msm_ispif_fns *ispif_fns;
};

/* abstract camera device represents a VFE and connected sensor */
struct msm_isp_ops {
	char *config_dev_name;

	/*int (*isp_init)(struct msm_cam_v4l2_device *pcam);*/
	int (*isp_open)(struct v4l2_subdev *sd, struct msm_sync *sync);
	int (*isp_config)(struct msm_cam_media_controller *pmctl,
		 unsigned int cmd, unsigned long arg);
	int (*isp_enqueue)(struct msm_cam_media_controller *pcam,
		struct msm_vfe_resp *data,
		enum msm_queue qtype);
	int (*isp_notify)(struct v4l2_subdev *sd, void *arg);

	void (*isp_release)(struct msm_sync *psync);

	/* vfe subdevice */
	struct v4l2_subdev sd;
};

struct msm_isp_buf_info {
	int type;
	unsigned long buffer;
	int fd;
};
#define MSM_DEV_INST_MAX                    8
struct msm_cam_v4l2_dev_inst {
	struct videobuf_queue vid_bufq;
	spinlock_t vb_irqlock;
	struct v4l2_format vid_fmt;
	/* senssor pixel code*/
	enum v4l2_mbus_pixelcode sensor_pxlcode;
	struct msm_cam_v4l2_device *pcam;
	int my_index;
	int image_mode;
	int path;
	int buf_count;
};
#define MSM_MAX_IMG_MODE                5
/* abstract camera device for each sensor successfully probed*/
struct msm_cam_v4l2_device {
	/* standard device interfaces */
	/* parent of video device to trace back */
	struct device dev;
	/* sensor's platform device*/
	struct platform_device *pdev;
	/* V4l2 device */
	struct v4l2_device v4l2_dev;
	/* will be registered as /dev/video*/
	struct video_device *pvdev;
	int use_count;
	/* will be used to init/release HW */
	struct msm_cam_media_controller mctl;
	/* sensor subdevice */
	struct v4l2_subdev sensor_sdev;
	struct msm_sensor_ctrl sctrl;

	/* parent device */
	struct device *parent_dev;

	struct mutex vid_lock;
	/* v4l2 format support */
	struct msm_isp_color_fmt *usr_fmts;
	int num_fmts;
	/* preview or snapshot */
	u32 mode;
	u32 memsize;

	int op_mode;
	int vnode_id;
	struct msm_cam_v4l2_dev_inst *dev_inst[MSM_DEV_INST_MAX];
	struct msm_cam_v4l2_dev_inst *dev_inst_map[MSM_MAX_IMG_MODE];
	/* native config device */
	struct cdev cdev;

	/* most-frequently accessed manager object*/
	struct msm_sync *sync;

	/* The message queue is used by the control thread to send commands
	 * to the config thread, and also by the HW to send messages to the
	 * config thread.  Thus it is the only queue that is accessed from
	 * both interrupt and process context.
	 */
	/* struct msm_device_queue event_q; */

	/* This queue used by the config thread to send responses back to the
	 * control thread.  It is accessed only from a process context.
	 * TO BE REMOVED
	 */
	struct msm_device_queue ctrl_q;

	struct mutex lock;
	uint8_t ctrl_data[max_control_command_size];
	struct msm_ctrl_cmd ctrl;
};
static inline struct msm_cam_v4l2_device *to_pcam(
	struct v4l2_device *v4l2_dev)
{
	return container_of(v4l2_dev, struct msm_cam_v4l2_device, v4l2_dev);
}

/*pseudo v4l2 device and v4l2 event queue
  for server and config cdevs*/
struct v4l2_queue_util {
	struct video_device *pvdev;
	struct v4l2_fh  eventHandle;
};

/* abstract config device for all sensor successfully probed*/
struct msm_cam_config_dev {
	struct cdev config_cdev;
	struct v4l2_queue_util config_stat_event_queue;
	int use_count;
	/*struct msm_isp_ops* isp_subdev;*/
	struct msm_cam_media_controller *p_mctl;
};

/* abstract camera server device for all sensor successfully probed*/
struct msm_cam_server_dev {

	/* config node device*/
	struct cdev server_cdev;
	/* info of sensors successfully probed*/
	struct msm_camera_info camera_info;
	/* info of configs successfully created*/
	struct msm_cam_config_dev_info config_info;
	/* active working camera device - only one allowed at this time*/
	struct msm_cam_v4l2_device *pcam_active;
	/* number of camera devices opened*/
	atomic_t number_pcam_active;
	struct v4l2_queue_util server_command_queue;
	/* This queue used by the config thread to send responses back to the
	 * control thread.  It is accessed only from a process context.
	 */
	struct msm_device_queue ctrl_q;
	uint8_t ctrl_data[max_control_command_size];
	struct msm_ctrl_cmd ctrl;
	int use_count;
    /* all the registered ISP subdevice*/
	struct msm_isp_ops *isp_subdev[MSM_MAX_CAMERA_CONFIGS];
	struct msm_ispif_fns ispif_fns;

};

/* camera server related functions */


/* ISP related functions */
void msm_isp_vfe_dev_init(struct v4l2_subdev *vd);
/*
int msm_isp_register(struct msm_cam_v4l2_device *pcam);
*/
int msm_isp_register(struct msm_cam_server_dev *psvr);
void msm_isp_unregister(struct msm_cam_server_dev *psvr);
int msm_ispif_register(struct msm_ispif_fns *ispif);
int msm_sensor_register(struct platform_device *pdev,
	int (*sensor_probe)(const struct msm_camera_sensor_info *,
	struct v4l2_subdev *, struct msm_sensor_ctrl *));
int msm_isp_init_module(int g_num_config_nodes);

int msm_mctl_init_module(struct msm_cam_v4l2_device *pcam);
int msm_mctl_init_user_formats(struct msm_cam_v4l2_device *pcam);
int msm_mctl_buf_done(struct msm_cam_media_controller *pmctl,
			int msg_type, uint32_t y_phy);
/*Memory(PMEM) functions*/
int msm_register_pmem(struct hlist_head *ptype, void __user *arg);
int msm_pmem_table_del(struct hlist_head *ptype, void __user *arg);
uint8_t msm_pmem_region_lookup(struct hlist_head *ptype,
	int pmem_type, struct msm_pmem_region *reg, uint8_t maxcount);
uint8_t msm_pmem_region_lookup_2(struct hlist_head *ptype,
					int pmem_type,
					struct msm_pmem_region *reg,
					uint8_t maxcount);
uint8_t msm_pmem_region_lookup_3(struct msm_cam_v4l2_device *pcam, int idx,
						struct msm_pmem_region *reg,
						uint8_t start_index,
						uint8_t stop_index,
						int mem_type);
unsigned long msm_pmem_stats_vtop_lookup(
				struct msm_sync *sync,
				unsigned long buffer,
				int fd);
unsigned long msm_pmem_stats_ptov_lookup(struct msm_sync *sync,
						unsigned long addr, int *fd);

int msm_vfe_subdev_init(struct v4l2_subdev *sd, void *data,
					struct platform_device *pdev);
void msm_vfe_subdev_release(struct platform_device *pdev);

int msm_isp_subdev_ioctl(struct v4l2_subdev *sd,
	struct msm_vfe_cfg_cmd *cfgcmd, void *data);
#endif /* __KERNEL__ */

#endif /* _MSM_H */
