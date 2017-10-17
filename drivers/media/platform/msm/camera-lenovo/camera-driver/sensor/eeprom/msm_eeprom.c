/* Copyright (c) 2011-2014, The Linux Foundation. All rights reserved.
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

#include <linux/module.h>
#include <linux/of_gpio.h>
#include <linux/delay.h>
#include <linux/crc32.h>
#include "msm_sd.h"
#include "msm_cci.h"
#include "msm_eeprom.h"
#include "../actuator/onsemi_ois.h"

#undef CDBG
//#define MSM_EEPROM_DEBUG
#ifdef MSM_EEPROM_DEBUG
#define CDBG(fmt, args...) pr_err(fmt, ##args)
#else
#define CDBG(fmt, args...) pr_debug(fmt, ##args)
#endif

DEFINE_MSM_MUTEX(msm_eeprom_mutex);
#ifdef CONFIG_COMPAT
static struct v4l2_file_operations msm_eeprom_v4l2_subdev_fops;
#endif

/*+begin lijk add eeprom checksum function 2014-07-11*/
/*lenovo-sw add for eeprom read only once*/
#define E2PROM_JUCHEN "juchen"
#define E2PROM_ONSEMI "onsemi"

#define IS_E2PROM_JUCHEN(x) if (!strcmp(x, E2PROM_JUCHEN))
#define IS_E2PROM_ONSEMI(x) if (!strcmp(x, E2PROM_ONSEMI))

#define THREEA_BEGIN_OFFSET 0
#define THREEA_END_OFFSET 2045
#define THREEA_CHKSUM_HI_OFFSET 2046
#define THREEA_CHKSUM_LOW_OFFSET 2047

#define OIS_BEGIN_OFFSET 2048
#define OIS_END_OFFSET 2100
#define OIS_CHKSUM_HI_OFFSET 2101
#define OIS_CHKSUM_LOW_OFFSET 2102

#define POSTURE_BEGIN_OFFSET 2113
#define POSTURE_END_OFFSET 2182
#define POSTURE_CHKSUM_HI_OFFSET 2183
#define POSTURE_CHKSUM_LOW_OFFSET 2184

uint8_t is_3a_checksumed = 0;
uint8_t is_ois_checksumed = 0;
uint8_t is_posture_checksumed = 0;

//add for front camera
#define FRONT_THREEA_BEGIN_OFFSET 0
#define FRONT_THREEA_END_OFFSET 1021
#define FRONT_THREEA_CHKSUM_HI_OFFSET 1022
#define FRONT_THREEA_CHKSUM_LOW_OFFSET 1023
uint8_t FRONT_is_3a_checksumed = 0;
uint8_t FRONT_is_ois_checksumed = 0;
extern int	E2pDat_Lenovo( uint8_t * memory_data);
struct msm_eeprom_ctrl_t *eeprom_rear_data_ctrl = NULL;
struct msm_eeprom_ctrl_t *eeprom_front_data_ctrl = NULL;
/*+ end*/

/*+end ljk add eeprom checksum function 2014-07-11*/

/*+Begin: chenglong1 add for passion eeprom 2015-05-07*/
#define E2PROM_SUNNY_F13S01M "sunny_f13s01m_dw9761b"

#define SUNNY_F13S01M_EEPROM_BASE 0x400

#define SUNNY_F13S01M_MODULE_INFO_OFFSET (0x400-SUNNY_F13S01M_EEPROM_BASE)
#define SUNNY_F13S01M_MODULE_INFO_SIZE 48 

#define SUNNY_F13S01M_AFC_OFFSET (0x432-SUNNY_F13S01M_EEPROM_BASE)
#define SUNNY_F13S01M_AFC_SIZE 132

#define SUNNY_F13S01M_WBC_OFFSET (0x4B8-SUNNY_F13S01M_EEPROM_BASE)
#define SUNNY_F13S01M_WBC_SIZE 49

#define SUNNY_F13S01M_LSC_OFFSET (0x4EB-SUNNY_F13S01M_EEPROM_BASE)
#define SUNNY_F13S01M_LSC_SIZE 1329

#define SUNNY_F13S01M_PDAF_GM_OFFSET (0x1100-SUNNY_F13S01M_EEPROM_BASE)
#define SUNNY_F13S01M_PDAF_GM_SIZE 1031

#define SUNNY_F13S01M_PDAF_COEF_OFFSET (0x1509-SUNNY_F13S01M_EEPROM_BASE)
#define SUNNY_F13S01M_PDAF_COEF_SIZE 3

uint8_t module_info_checksum_pass = 0;
uint8_t afc_checksum_pass = 0;
uint8_t wbc_checksum_pass = 0;
uint8_t lsc_checksum_pass = 0;
uint8_t pdaf_gain_map_checksum_pass = 0;
uint8_t pdaf_conv_coef_checksum_pass = 0;

static uint16_t sunny_f13s01m_dw9761_calc_checksum(uint8_t *pData, uint32_t len)
{
	uint32_t i;
	uint32_t sum =0;
	for (i=0; i<len; i++) {
		sum += *pData++;
	}

	return (uint16_t)sum;
}
/*+End.*/

/**
  * msm_eeprom_verify_sum - verify crc32 checksum
  * @mem:	data buffer
  * @size:	size of data buffer
  * @sum:	expected checksum
  *
  * Returns 0 if checksum match, -EINVAL otherwise.
  */
static int msm_eeprom_verify_sum(const char *mem, uint32_t size, uint32_t sum)
{
	uint32_t crc = ~0;

	/* check overflow */
	if (size > crc - sizeof(uint32_t))
		return -EINVAL;

	crc = crc32_le(crc, mem, size);
	if (~crc != sum) {
		CDBG("%s: expect 0x%x, result 0x%x\n", __func__, sum, ~crc);
		return -EINVAL;
	}
	CDBG("%s: checksum pass 0x%x\n", __func__, sum);
	return 0;
}

/**
  * msm_eeprom_match_crc - verify multiple regions using crc
  * @data:	data block to be verified
  *
  * Iterates through all regions stored in @data.  Regions with odd index
  * are treated as data, and its next region is treated as checksum.  Thus
  * regions of even index must have valid_size of 4 or 0 (skip verification).
  * Returns a bitmask of verified regions, starting from LSB.  1 indicates
  * a checksum match, while 0 indicates checksum mismatch or not verified.
  */
static uint32_t msm_eeprom_match_crc(struct msm_eeprom_memory_block_t *data)
{
	int j, rc;
	uint32_t *sum;
	uint32_t ret = 0;
	uint8_t *memptr;
	struct msm_eeprom_memory_map_t *map;

	if (!data) {
		pr_err("%s data is NULL", __func__);
		return -EINVAL;
	}
	map = data->map;
	memptr = data->mapdata;

	for (j = 0; j + 1 < data->num_map; j += 2) {
		/* empty table or no checksum */
		if (!map[j].mem.valid_size || !map[j+1].mem.valid_size) {
			memptr += map[j].mem.valid_size
				+ map[j+1].mem.valid_size;
			continue;
		}
		if (map[j+1].mem.valid_size != sizeof(uint32_t)) {
			CDBG("%s: malformatted data mapping\n", __func__);
			return -EINVAL;
		}
		sum = (uint32_t *) (memptr + map[j].mem.valid_size);
		rc = msm_eeprom_verify_sum(memptr, map[j].mem.valid_size,
					   *sum);
		if (!rc)
			ret |= 1 << (j/2);
		memptr += map[j].mem.valid_size + map[j+1].mem.valid_size;
	}
	return ret;
}

static int msm_eeprom_get_cmm_data(struct msm_eeprom_ctrl_t *e_ctrl,
				       struct msm_eeprom_cfg_data *cdata)
{
	int rc = 0;
	struct msm_eeprom_cmm_t *cmm_data = &e_ctrl->eboard_info->cmm_data;
	cdata->cfg.get_cmm_data.cmm_support = cmm_data->cmm_support;
	cdata->cfg.get_cmm_data.cmm_compression = cmm_data->cmm_compression;
	cdata->cfg.get_cmm_data.cmm_size = cmm_data->cmm_size;
	return rc;
}

static int eeprom_config_read_cal_data(struct msm_eeprom_ctrl_t *e_ctrl,
	struct msm_eeprom_cfg_data *cdata)
{
	int rc;

	/* check range */
	if (cdata->cfg.read_data.num_bytes >
		e_ctrl->cal_data.num_data) {
		CDBG("%s: Invalid size. exp %u, req %u\n", __func__,
			e_ctrl->cal_data.num_data,
			cdata->cfg.read_data.num_bytes);
		return -EINVAL;
	}
	if (!e_ctrl->cal_data.mapdata)
		return -EFAULT;

	rc = copy_to_user(cdata->cfg.read_data.dbuffer,
		e_ctrl->cal_data.mapdata,
		cdata->cfg.read_data.num_bytes);

	return rc;
}

static int msm_eeprom_config(struct msm_eeprom_ctrl_t *e_ctrl,
	void __user *argp)
{
	struct msm_eeprom_cfg_data *cdata =
		(struct msm_eeprom_cfg_data *)argp;
	int rc = 0;

	CDBG("%s E\n", __func__);
	switch (cdata->cfgtype) {
	case CFG_EEPROM_GET_INFO:
		CDBG("%s E CFG_EEPROM_GET_INFO\n", __func__);
		cdata->is_supported = e_ctrl->is_supported;
		memcpy(cdata->cfg.eeprom_name,
			e_ctrl->eboard_info->eeprom_name,
			sizeof(cdata->cfg.eeprom_name));
		break;
	case CFG_EEPROM_GET_CAL_DATA:
		CDBG("%s E CFG_EEPROM_GET_CAL_DATA\n", __func__);
		cdata->cfg.get_data.num_bytes =
			e_ctrl->cal_data.num_data;
		break;
	case CFG_EEPROM_READ_CAL_DATA:
		CDBG("%s E CFG_EEPROM_READ_CAL_DATA\n", __func__);
		rc = eeprom_config_read_cal_data(e_ctrl, cdata);
		break;
	case CFG_EEPROM_GET_MM_INFO:
		CDBG("%s E CFG_EEPROM_GET_MM_INFO\n", __func__);
		rc = msm_eeprom_get_cmm_data(e_ctrl, cdata);
		break;
	default:
		break;
	}

	CDBG("%s X rc: %d\n", __func__, rc);
	return rc;
}

static int msm_eeprom_get_subdev_id(struct msm_eeprom_ctrl_t *e_ctrl,
				    void *arg)
{
	uint32_t *subdev_id = (uint32_t *)arg;
	CDBG("%s E\n", __func__);
	if (!subdev_id) {
		pr_err("%s failed\n", __func__);
		return -EINVAL;
	}
	*subdev_id = e_ctrl->subdev_id;
	CDBG("subdev_id %d\n", *subdev_id);
	CDBG("%s X\n", __func__);
	return 0;
}

static long msm_eeprom_subdev_ioctl(struct v4l2_subdev *sd,
		unsigned int cmd, void *arg)
{
	struct msm_eeprom_ctrl_t *e_ctrl = v4l2_get_subdevdata(sd);
	void __user *argp = (void __user *)arg;
	CDBG("%s E\n", __func__);
	CDBG("%s:%d a_ctrl %p argp %p\n", __func__, __LINE__, e_ctrl, argp);
	switch (cmd) {
	case VIDIOC_MSM_SENSOR_GET_SUBDEV_ID:
		return msm_eeprom_get_subdev_id(e_ctrl, argp);
	case VIDIOC_MSM_EEPROM_CFG:
		return msm_eeprom_config(e_ctrl, argp);
	default:
		return -ENOIOCTLCMD;
	}

	CDBG("%s X\n", __func__);
}

static struct msm_camera_i2c_fn_t msm_eeprom_cci_func_tbl = {
	.i2c_read = msm_camera_cci_i2c_read,
	.i2c_read_seq = msm_camera_cci_i2c_read_seq,
	.i2c_write = msm_camera_cci_i2c_write,
	.i2c_write_seq = msm_camera_cci_i2c_write_seq,
	.i2c_write_table = msm_camera_cci_i2c_write_table,
	.i2c_write_seq_table = msm_camera_cci_i2c_write_seq_table,
	.i2c_write_table_w_microdelay =
	msm_camera_cci_i2c_write_table_w_microdelay,
	.i2c_util = msm_sensor_cci_i2c_util,
	.i2c_poll = msm_camera_cci_i2c_poll,
};

static struct msm_camera_i2c_fn_t msm_eeprom_qup_func_tbl = {
	.i2c_read = msm_camera_qup_i2c_read,
	.i2c_read_seq = msm_camera_qup_i2c_read_seq,
	.i2c_write = msm_camera_qup_i2c_write,
	.i2c_write_table = msm_camera_qup_i2c_write_table,
	.i2c_write_seq_table = msm_camera_qup_i2c_write_seq_table,
	.i2c_write_table_w_microdelay =
	msm_camera_qup_i2c_write_table_w_microdelay,
};

static struct msm_camera_i2c_fn_t msm_eeprom_spi_func_tbl = {
	.i2c_read = msm_camera_spi_read,
	.i2c_read_seq = msm_camera_spi_read_seq,
};

static int msm_eeprom_open(struct v4l2_subdev *sd,
	struct v4l2_subdev_fh *fh) {
	int rc = 0;
	struct msm_eeprom_ctrl_t *e_ctrl =  v4l2_get_subdevdata(sd);
	CDBG("%s E\n", __func__);
	if (!e_ctrl) {
		pr_err("%s failed e_ctrl is NULL\n", __func__);
		return -EINVAL;
	}
	CDBG("%s X\n", __func__);
	return rc;
}

static int msm_eeprom_close(struct v4l2_subdev *sd,
	struct v4l2_subdev_fh *fh) {
	int rc = 0;
	struct msm_eeprom_ctrl_t *e_ctrl =  v4l2_get_subdevdata(sd);
	CDBG("%s E\n", __func__);
	if (!e_ctrl) {
		pr_err("%s failed e_ctrl is NULL\n", __func__);
		return -EINVAL;
	}
	CDBG("%s X\n", __func__);
	return rc;
}

static const struct v4l2_subdev_internal_ops msm_eeprom_internal_ops = {
	.open = msm_eeprom_open,
	.close = msm_eeprom_close,
};
/**
  * read_eeprom_memory() - read map data into buffer
  * @e_ctrl:	eeprom control struct
  * @block:	block to be read
  *
  * This function iterates through blocks stored in block->map, reads each
  * region and concatenate them into the pre-allocated block->mapdata
  */
static int read_eeprom_memory(struct msm_eeprom_ctrl_t *e_ctrl,
			      struct msm_eeprom_memory_block_t *block)
{
	int rc = 0;
	int j;
	struct msm_eeprom_memory_map_t *emap = block->map;
	struct msm_eeprom_board_info *eb_info;
	uint8_t *memptr = block->mapdata;

	if (!e_ctrl) {
		pr_err("%s e_ctrl is NULL", __func__);
		return -EINVAL;
	}

	eb_info = e_ctrl->eboard_info;

	for (j = 0; j < block->num_map; j++) {
		if (emap[j].saddr.addr) {
			eb_info->i2c_slaveaddr = emap[j].saddr.addr;
			e_ctrl->i2c_client.cci_client->sid =
					eb_info->i2c_slaveaddr >> 1;
			pr_err("qcom,slave-addr = 0x%X\n",
				eb_info->i2c_slaveaddr);
		}

		if (emap[j].page.valid_size) {
			e_ctrl->i2c_client.addr_type = emap[j].page.addr_t;
			rc = e_ctrl->i2c_client.i2c_func_tbl->i2c_write(
				&(e_ctrl->i2c_client), emap[j].page.addr,
				emap[j].page.data, emap[j].page.data_t);
				msleep(emap[j].page.delay);
			if (rc < 0) {
				pr_err("%s: page write failed\n", __func__);
				return rc;
			}
		}
		if (emap[j].pageen.valid_size) {
			e_ctrl->i2c_client.addr_type = emap[j].pageen.addr_t;
			rc = e_ctrl->i2c_client.i2c_func_tbl->i2c_write(
				&(e_ctrl->i2c_client), emap[j].pageen.addr,
				emap[j].pageen.data, emap[j].pageen.data_t);
				msleep(emap[j].pageen.delay);
			if (rc < 0) {
				pr_err("%s: page enable failed\n", __func__);
				return rc;
			}
		}
		if (emap[j].poll.valid_size) {
			e_ctrl->i2c_client.addr_type = emap[j].poll.addr_t;
			rc = e_ctrl->i2c_client.i2c_func_tbl->i2c_poll(
				&(e_ctrl->i2c_client), emap[j].poll.addr,
				emap[j].poll.data, emap[j].poll.data_t);
				msleep(emap[j].poll.delay);
			if (rc < 0) {
				pr_err("%s: poll failed\n", __func__);
				return rc;
			}
		}

		if (emap[j].mem.valid_size) {
			e_ctrl->i2c_client.addr_type = emap[j].mem.addr_t;
			rc = e_ctrl->i2c_client.i2c_func_tbl->i2c_read_seq(
				&(e_ctrl->i2c_client), emap[j].mem.addr,
				memptr, emap[j].mem.valid_size);
			if (rc < 0) {
				pr_err("%s: read failed\n", __func__);
				return rc;
			}
			memptr += emap[j].mem.valid_size;
		}
		if (emap[j].pageen.valid_size) {
			e_ctrl->i2c_client.addr_type = emap[j].pageen.addr_t;
			rc = e_ctrl->i2c_client.i2c_func_tbl->i2c_write(
				&(e_ctrl->i2c_client), emap[j].pageen.addr,
				0, emap[j].pageen.data_t);
			if (rc < 0) {
				pr_err("%s: page disable failed\n", __func__);
				return rc;
			}
		}
	}
	return rc;
}
/**
  * msm_eeprom_parse_memory_map() - parse memory map in device node
  * @of:	device node
  * @data:	memory block for output
  *
  * This functions parses @of to fill @data.  It allocates map itself, parses
  * the @of node, calculate total data length, and allocates required buffer.
  * It only fills the map, but does not perform actual reading.
  */
static int msm_eeprom_parse_memory_map(struct device_node *of,
				       struct msm_eeprom_memory_block_t *data)
{
	int i, rc = 0;
	char property[PROPERTY_MAXSIZE];
	uint32_t count = 6;
	struct msm_eeprom_memory_map_t *map;

	snprintf(property, PROPERTY_MAXSIZE, "qcom,num-blocks");
	rc = of_property_read_u32(of, property, &data->num_map);
	CDBG("%s: %s %d\n", __func__, property, data->num_map);
	if (rc < 0) {
		pr_err("%s failed rc %d\n", __func__, rc);
		return rc;
	}

	map = kzalloc((sizeof(*map) * data->num_map), GFP_KERNEL);
	if (!map) {
		pr_err("%s failed line %d\n", __func__, __LINE__);
		return -ENOMEM;
	}
	data->map = map;

	for (i = 0; i < data->num_map; i++) {
		snprintf(property, PROPERTY_MAXSIZE, "qcom,page%d", i);
		rc = of_property_read_u32_array(of, property,
				(uint32_t *) &map[i].page, count);
		if (rc < 0) {
			pr_err("%s: failed %d\n", __func__, __LINE__);
			goto ERROR;
		}

		snprintf(property, PROPERTY_MAXSIZE,
					"qcom,pageen%d", i);
		rc = of_property_read_u32_array(of, property,
			(uint32_t *) &map[i].pageen, count);
		if (rc < 0)
			pr_err("%s: pageen not needed\n", __func__);

		snprintf(property, PROPERTY_MAXSIZE, "qcom,saddr%d", i);
		rc = of_property_read_u32_array(of, property,
			(uint32_t *) &map[i].saddr.addr, 1);
		if (rc < 0)
			CDBG("%s: saddr not needed - block %d\n", __func__, i);

		snprintf(property, PROPERTY_MAXSIZE, "qcom,poll%d", i);
		rc = of_property_read_u32_array(of, property,
				(uint32_t *) &map[i].poll, count);
		if (rc < 0) {
			pr_err("%s failed %d\n", __func__, __LINE__);
			goto ERROR;
		}

		snprintf(property, PROPERTY_MAXSIZE, "qcom,mem%d", i);
		rc = of_property_read_u32_array(of, property,
				(uint32_t *) &map[i].mem, count);
		if (rc < 0) {
			pr_err("%s failed %d\n", __func__, __LINE__);
			goto ERROR;
		}
		data->num_data += map[i].mem.valid_size;
	}

	CDBG("%s num_bytes %d\n", __func__, data->num_data);

	data->mapdata = kzalloc(data->num_data, GFP_KERNEL);
	if (!data->mapdata) {
		pr_err("%s failed line %d\n", __func__, __LINE__);
		rc = -ENOMEM;
		goto ERROR;
	}
	return rc;

ERROR:
	kfree(data->map);
	memset(data, 0, sizeof(*data));
	return rc;
}

static struct msm_cam_clk_info cam_8960_clk_info[] = {
	[SENSOR_CAM_MCLK] = {"cam_clk", 24000000},
};

static struct msm_cam_clk_info cam_8974_clk_info[] = {
	[SENSOR_CAM_MCLK] = {"cam_src_clk", 19200000},
	[SENSOR_CAM_CLK] = {"cam_clk", 0},
};

static struct v4l2_subdev_core_ops msm_eeprom_subdev_core_ops = {
	.ioctl = msm_eeprom_subdev_ioctl,
};

static struct v4l2_subdev_ops msm_eeprom_subdev_ops = {
	.core = &msm_eeprom_subdev_core_ops,
};

static int msm_eeprom_i2c_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	int rc = 0;
	struct msm_eeprom_ctrl_t *e_ctrl = NULL;
	struct msm_camera_power_ctrl_t *power_info = NULL;
	CDBG("%s E\n", __func__);

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		pr_err("%s i2c_check_functionality failed\n", __func__);
		goto probe_failure;
	}

	e_ctrl = kzalloc(sizeof(*e_ctrl), GFP_KERNEL);
	if (!e_ctrl) {
		pr_err("%s:%d kzalloc failed\n", __func__, __LINE__);
		return -ENOMEM;
	}
	e_ctrl->eeprom_v4l2_subdev_ops = &msm_eeprom_subdev_ops;
	e_ctrl->eeprom_mutex = &msm_eeprom_mutex;
	CDBG("%s client = 0x%p\n", __func__, client);
	e_ctrl->eboard_info = (struct msm_eeprom_board_info *)(id->driver_data);
	if (!e_ctrl->eboard_info) {
		pr_err("%s:%d board info NULL\n", __func__, __LINE__);
		rc = -EINVAL;
		goto ectrl_free;
	}
	power_info = &e_ctrl->eboard_info->power_info;
	e_ctrl->i2c_client.client = client;

	/* Set device type as I2C */
	e_ctrl->eeprom_device_type = MSM_CAMERA_I2C_DEVICE;
	e_ctrl->i2c_client.i2c_func_tbl = &msm_eeprom_qup_func_tbl;

	if (e_ctrl->eboard_info->i2c_slaveaddr != 0)
		e_ctrl->i2c_client.client->addr =
					e_ctrl->eboard_info->i2c_slaveaddr;
	power_info->clk_info = cam_8960_clk_info;
	power_info->clk_info_size = ARRAY_SIZE(cam_8960_clk_info);
	power_info->dev = &client->dev;

	/*IMPLEMENT READING PART*/
	/* Initialize sub device */
	v4l2_i2c_subdev_init(&e_ctrl->msm_sd.sd,
		e_ctrl->i2c_client.client,
		e_ctrl->eeprom_v4l2_subdev_ops);
	v4l2_set_subdevdata(&e_ctrl->msm_sd.sd, e_ctrl);
	e_ctrl->msm_sd.sd.internal_ops = &msm_eeprom_internal_ops;
	e_ctrl->msm_sd.sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	media_entity_init(&e_ctrl->msm_sd.sd.entity, 0, NULL, 0);
	e_ctrl->msm_sd.sd.entity.type = MEDIA_ENT_T_V4L2_SUBDEV;
	e_ctrl->msm_sd.sd.entity.group_id = MSM_CAMERA_SUBDEV_EEPROM;
	msm_sd_register(&e_ctrl->msm_sd);
	CDBG("%s success result=%d X\n", __func__, rc);
	return rc;

ectrl_free:
	kfree(e_ctrl);
probe_failure:
	pr_err("%s failed! rc = %d\n", __func__, rc);
	return rc;
}

static int msm_eeprom_i2c_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct msm_eeprom_ctrl_t  *e_ctrl;
	if (!sd) {
		pr_err("%s: Subdevice is NULL\n", __func__);
		return 0;
	}

	e_ctrl = (struct msm_eeprom_ctrl_t *)v4l2_get_subdevdata(sd);
	if (!e_ctrl) {
		pr_err("%s: eeprom device is NULL\n", __func__);
		return 0;
	}

	kfree(e_ctrl->cal_data.mapdata);
	kfree(e_ctrl->cal_data.map);
	if (e_ctrl->eboard_info) {
		kfree(e_ctrl->eboard_info->power_info.gpio_conf);
		kfree(e_ctrl->eboard_info);
	}
	kfree(e_ctrl);
	return 0;
}

#define msm_eeprom_spi_parse_cmd(spic, str, name, out, size)		\
	{								\
		if (of_property_read_u32_array(				\
			spic->spi_master->dev.of_node,			\
			str, out, size)) {				\
			return -EFAULT;					\
		} else {						\
			spic->cmd_tbl.name.opcode = out[0];		\
			spic->cmd_tbl.name.addr_len = out[1];		\
			spic->cmd_tbl.name.dummy_len = out[2];		\
		}							\
	}

static int msm_eeprom_spi_parse_of(struct msm_camera_spi_client *spic)
{
	int rc = -EFAULT;
	uint32_t tmp[3];
	msm_eeprom_spi_parse_cmd(spic, "qcom,spiop,read", read, tmp, 3);
	msm_eeprom_spi_parse_cmd(spic, "qcom,spiop,readseq", read_seq, tmp, 3);
	msm_eeprom_spi_parse_cmd(spic, "qcom,spiop,queryid", query_id, tmp, 3);

	rc = of_property_read_u32_array(spic->spi_master->dev.of_node,
					"qcom,eeprom-id", tmp, 2);
	if (rc) {
		pr_err("%s: Failed to get eeprom id\n", __func__);
		return rc;
	}
	spic->mfr_id0 = tmp[0];
	spic->device_id0 = tmp[1];

	return 0;
}

static int msm_eeprom_match_id(struct msm_eeprom_ctrl_t *e_ctrl)
{
	int rc;
	struct msm_camera_i2c_client *client = &e_ctrl->i2c_client;
	uint8_t id[2];

	rc = msm_camera_spi_query_id(client, 0, &id[0], 2);
	if (rc < 0)
		return rc;
	CDBG("%s: read 0x%x 0x%x, check 0x%x 0x%x\n", __func__, id[0],
		id[1], client->spi_client->mfr_id0,
			client->spi_client->device_id0);
	if (id[0] != client->spi_client->mfr_id0
		    || id[1] != client->spi_client->device_id0)
		return -ENODEV;

	return 0;
}

static int msm_eeprom_get_dt_data(struct msm_eeprom_ctrl_t *e_ctrl)
{
	int rc = 0, i = 0;
	struct msm_eeprom_board_info *eb_info;
	struct msm_camera_power_ctrl_t *power_info =
		&e_ctrl->eboard_info->power_info;
	struct device_node *of_node = NULL;
	struct msm_camera_gpio_conf *gconf = NULL;
	uint16_t gpio_array_size = 0;
	uint16_t *gpio_array = NULL;

	eb_info = e_ctrl->eboard_info;
	if (e_ctrl->eeprom_device_type == MSM_CAMERA_SPI_DEVICE)
		of_node = e_ctrl->i2c_client.
			spi_client->spi_master->dev.of_node;
	else if (e_ctrl->eeprom_device_type == MSM_CAMERA_PLATFORM_DEVICE)
		of_node = e_ctrl->pdev->dev.of_node;

	if (!of_node) {
		pr_err("%s: %d of_node is NULL\n", __func__ , __LINE__);
		return -ENOMEM;
	}
	rc = msm_camera_get_dt_vreg_data(of_node, &power_info->cam_vreg,
					     &power_info->num_vreg);
	if (rc < 0)
		return rc;

	rc = msm_camera_get_dt_power_setting_data(of_node,
		power_info->cam_vreg, power_info->num_vreg,
		power_info);
	if (rc < 0)
		goto ERROR1;

	power_info->gpio_conf = kzalloc(sizeof(struct msm_camera_gpio_conf),
					GFP_KERNEL);
	if (!power_info->gpio_conf) {
		rc = -ENOMEM;
		goto ERROR2;
	}
	gconf = power_info->gpio_conf;
	gpio_array_size = of_gpio_count(of_node);
	CDBG("%s gpio count %d\n", __func__, gpio_array_size);

	if (gpio_array_size) {
		gpio_array = kzalloc(sizeof(uint16_t) * gpio_array_size,
			GFP_KERNEL);
		if (!gpio_array) {
			pr_err("%s failed %d\n", __func__, __LINE__);
			goto ERROR3;
		}
		for (i = 0; i < gpio_array_size; i++) {
			gpio_array[i] = of_get_gpio(of_node, i);
			CDBG("%s gpio_array[%d] = %d\n", __func__, i,
				gpio_array[i]);
		}

		rc = msm_camera_get_dt_gpio_req_tbl(of_node, gconf,
			gpio_array, gpio_array_size);
		if (rc < 0) {
			pr_err("%s failed %d\n", __func__, __LINE__);
			goto ERROR4;
		}

		rc = msm_camera_init_gpio_pin_tbl(of_node, gconf,
			gpio_array, gpio_array_size);
		if (rc < 0) {
			pr_err("%s failed %d\n", __func__, __LINE__);
			goto ERROR4;
		}
		kfree(gpio_array);
	}

	return rc;
ERROR4:
	kfree(gpio_array);
ERROR3:
	kfree(power_info->gpio_conf);
ERROR2:
	kfree(power_info->cam_vreg);
ERROR1:
	kfree(power_info->power_setting);
	return rc;
}


static int msm_eeprom_cmm_dts(struct msm_eeprom_board_info *eb_info,
				struct device_node *of_node)
{
	int rc = 0;
	struct msm_eeprom_cmm_t *cmm_data = &eb_info->cmm_data;

	cmm_data->cmm_support =
		of_property_read_bool(of_node, "qcom,cmm-data-support");
	if (!cmm_data->cmm_support)
		return -EINVAL;
	cmm_data->cmm_compression =
		of_property_read_bool(of_node, "qcom,cmm-data-compressed");
	if (!cmm_data->cmm_compression)
		CDBG("No MM compression data\n");

	rc = of_property_read_u32(of_node, "qcom,cmm-data-offset",
				  &cmm_data->cmm_offset);
	if (rc < 0)
		CDBG("No MM offset data\n");

	rc = of_property_read_u32(of_node, "qcom,cmm-data-size",
				  &cmm_data->cmm_size);
	if (rc < 0)
		CDBG("No MM size data\n");

	CDBG("cmm_support: cmm_compr %d, cmm_offset %d, cmm_size %d\n",
		cmm_data->cmm_compression,
		cmm_data->cmm_offset,
		cmm_data->cmm_size);
	return 0;
}

static int msm_eeprom_spi_setup(struct spi_device *spi)
{
	struct msm_eeprom_ctrl_t *e_ctrl = NULL;
	struct msm_camera_i2c_client *client = NULL;
	struct msm_camera_spi_client *spi_client;
	struct msm_eeprom_board_info *eb_info;
	struct msm_camera_power_ctrl_t *power_info = NULL;
	int rc = 0;

	e_ctrl = kzalloc(sizeof(*e_ctrl), GFP_KERNEL);
	if (!e_ctrl) {
		pr_err("%s:%d kzalloc failed\n", __func__, __LINE__);
		return -ENOMEM;
	}
	e_ctrl->eeprom_v4l2_subdev_ops = &msm_eeprom_subdev_ops;
	e_ctrl->eeprom_mutex = &msm_eeprom_mutex;
	client = &e_ctrl->i2c_client;
	e_ctrl->is_supported = 0;

	spi_client = kzalloc(sizeof(*spi_client), GFP_KERNEL);
	if (!spi_client) {
		pr_err("%s:%d kzalloc failed\n", __func__, __LINE__);
		kfree(e_ctrl);
		return -ENOMEM;
	}

	rc = of_property_read_u32(spi->dev.of_node, "cell-index",
				  &e_ctrl->subdev_id);
	CDBG("cell-index %d, rc %d\n", e_ctrl->subdev_id, rc);
	if (rc < 0) {
		pr_err("failed rc %d\n", rc);
		return rc;
	}

	e_ctrl->eeprom_device_type = MSM_CAMERA_SPI_DEVICE;
	client->spi_client = spi_client;
	spi_client->spi_master = spi;
	client->i2c_func_tbl = &msm_eeprom_spi_func_tbl;
	client->addr_type = MSM_CAMERA_I2C_3B_ADDR;

	eb_info = kzalloc(sizeof(*eb_info), GFP_KERNEL);
	if (!eb_info)
		goto spi_free;
	e_ctrl->eboard_info = eb_info;
	rc = of_property_read_string(spi->dev.of_node, "qcom,eeprom-name",
		&eb_info->eeprom_name);
	CDBG("%s qcom,eeprom-name %s, rc %d\n", __func__,
		eb_info->eeprom_name, rc);
	if (rc < 0) {
		pr_err("%s failed %d\n", __func__, __LINE__);
		goto board_free;
	}

	rc = msm_eeprom_cmm_dts(e_ctrl->eboard_info, spi->dev.of_node);
	if (rc < 0)
		CDBG("%s MM data miss:%d\n", __func__, __LINE__);

	power_info = &eb_info->power_info;

	power_info->clk_info = cam_8974_clk_info;
	power_info->clk_info_size = ARRAY_SIZE(cam_8974_clk_info);
	power_info->dev = &spi->dev;

	rc = msm_eeprom_get_dt_data(e_ctrl);
	if (rc < 0)
		goto board_free;

	/* set spi instruction info */
	spi_client->retry_delay = 1;
	spi_client->retries = 0;

	rc = msm_eeprom_spi_parse_of(spi_client);
	if (rc < 0) {
		dev_err(&spi->dev,
			"%s: Error parsing device properties\n", __func__);
		goto board_free;
	}

	/* prepare memory buffer */
	rc = msm_eeprom_parse_memory_map(spi->dev.of_node,
					 &e_ctrl->cal_data);
	if (rc < 0)
		CDBG("%s: no cal memory map\n", __func__);

	/* power up eeprom for reading */
	rc = msm_camera_power_up(power_info, e_ctrl->eeprom_device_type,
		&e_ctrl->i2c_client);
	if (rc < 0) {
		pr_err("failed rc %d\n", rc);
		goto caldata_free;
	}

	/* check eeprom id */
	rc = msm_eeprom_match_id(e_ctrl);
	if (rc < 0) {
		CDBG("%s: eeprom not matching %d\n", __func__, rc);
		goto power_down;
	}
	/* read eeprom */
	if (e_ctrl->cal_data.map) {
		rc = read_eeprom_memory(e_ctrl, &e_ctrl->cal_data);
		if (rc < 0) {
			pr_err("%s: read cal data failed\n", __func__);
			goto power_down;
		}
		e_ctrl->is_supported |= msm_eeprom_match_crc(
						&e_ctrl->cal_data);
	}

	rc = msm_camera_power_down(power_info, e_ctrl->eeprom_device_type,
		&e_ctrl->i2c_client);
	if (rc < 0) {
		pr_err("failed rc %d\n", rc);
		goto caldata_free;
	}

	/* initiazlie subdev */
	v4l2_spi_subdev_init(&e_ctrl->msm_sd.sd,
		e_ctrl->i2c_client.spi_client->spi_master,
		e_ctrl->eeprom_v4l2_subdev_ops);
	v4l2_set_subdevdata(&e_ctrl->msm_sd.sd, e_ctrl);
	e_ctrl->msm_sd.sd.internal_ops = &msm_eeprom_internal_ops;
	e_ctrl->msm_sd.sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	media_entity_init(&e_ctrl->msm_sd.sd.entity, 0, NULL, 0);
	e_ctrl->msm_sd.sd.entity.type = MEDIA_ENT_T_V4L2_SUBDEV;
	e_ctrl->msm_sd.sd.entity.group_id = MSM_CAMERA_SUBDEV_EEPROM;
	msm_sd_register(&e_ctrl->msm_sd);
	e_ctrl->is_supported = (e_ctrl->is_supported << 1) | 1;
	CDBG("%s success result=%d supported=%x X\n", __func__, rc,
	     e_ctrl->is_supported);

	return 0;

power_down:
	msm_camera_power_down(power_info, e_ctrl->eeprom_device_type,
		&e_ctrl->i2c_client);
caldata_free:
	kfree(e_ctrl->cal_data.mapdata);
	kfree(e_ctrl->cal_data.map);
board_free:
	kfree(e_ctrl->eboard_info);
spi_free:
	kfree(spi_client);
	kfree(e_ctrl);
	return rc;
}

static int msm_eeprom_spi_probe(struct spi_device *spi)
{
	int irq, cs, cpha, cpol, cs_high;

	CDBG("%s\n", __func__);
	spi->bits_per_word = 8;
	spi->mode = SPI_MODE_0;
	spi_setup(spi);

	irq = spi->irq;
	cs = spi->chip_select;
	cpha = (spi->mode & SPI_CPHA) ? 1 : 0;
	cpol = (spi->mode & SPI_CPOL) ? 1 : 0;
	cs_high = (spi->mode & SPI_CS_HIGH) ? 1 : 0;
	CDBG("%s: irq[%d] cs[%x] CPHA[%x] CPOL[%x] CS_HIGH[%x]\n",
			__func__, irq, cs, cpha, cpol, cs_high);
	CDBG("%s: max_speed[%u]\n", __func__, spi->max_speed_hz);

	return msm_eeprom_spi_setup(spi);
}

static int msm_eeprom_spi_remove(struct spi_device *sdev)
{
	struct v4l2_subdev *sd = spi_get_drvdata(sdev);
	struct msm_eeprom_ctrl_t  *e_ctrl;
	if (!sd) {
		pr_err("%s: Subdevice is NULL\n", __func__);
		return 0;
	}

	e_ctrl = (struct msm_eeprom_ctrl_t *)v4l2_get_subdevdata(sd);
	if (!e_ctrl) {
		pr_err("%s: eeprom device is NULL\n", __func__);
		return 0;
	}

	kfree(e_ctrl->i2c_client.spi_client);
	kfree(e_ctrl->cal_data.mapdata);
	kfree(e_ctrl->cal_data.map);
	if (e_ctrl->eboard_info) {
		kfree(e_ctrl->eboard_info->power_info.gpio_conf);
		kfree(e_ctrl->eboard_info);
	}
	kfree(e_ctrl);
	return 0;
}

#ifdef CONFIG_COMPAT
static int eeprom_config_read_cal_data32(struct msm_eeprom_ctrl_t *e_ctrl,
	void __user *arg)
{
	int rc;
	uint8_t *ptr_dest = NULL;
	struct msm_eeprom_cfg_data32 *cdata32 =
		(struct msm_eeprom_cfg_data32 *) arg;
	struct msm_eeprom_cfg_data cdata;

	cdata.cfgtype = cdata32->cfgtype;
	cdata.is_supported = cdata32->is_supported;
	cdata.cfg.read_data.num_bytes = cdata32->cfg.read_data.num_bytes;
	/* check range */
	if (cdata.cfg.read_data.num_bytes >
	    e_ctrl->cal_data.num_data) {
		CDBG("%s: Invalid size. exp %u, req %u\n", __func__,
			e_ctrl->cal_data.num_data,
			cdata.cfg.read_data.num_bytes);
		return -EINVAL;
	}
	if (!e_ctrl->cal_data.mapdata)
		return -EFAULT;

	ptr_dest = (uint8_t *) compat_ptr(cdata32->cfg.read_data.dbuffer);

	rc = copy_to_user(ptr_dest, e_ctrl->cal_data.mapdata,
		cdata.cfg.read_data.num_bytes);

	/* should only be called once.  free kernel resource */
/*+Begin: libm1. comment out for camera work normal once mm-qcamera-daemon killed  PIKACHU-2027 2016-03-08 */
#if 0
	if (!rc) {
		kfree(e_ctrl->cal_data.mapdata);
		kfree(e_ctrl->cal_data.map);
		memset(&e_ctrl->cal_data, 0, sizeof(e_ctrl->cal_data));
	}
#endif
/*+End. */
	return rc;
}

static int msm_eeprom_config32(struct msm_eeprom_ctrl_t *e_ctrl,
	void __user *argp)
{
	struct msm_eeprom_cfg_data *cdata = (struct msm_eeprom_cfg_data *)argp;
	int rc = 0;

	CDBG("%s E\n", __func__);
	switch (cdata->cfgtype) {
	case CFG_EEPROM_GET_INFO:
		CDBG("%s E CFG_EEPROM_GET_INFO\n", __func__);
		cdata->is_supported = e_ctrl->is_supported;
		memcpy(cdata->cfg.eeprom_name,
			e_ctrl->eboard_info->eeprom_name,
			sizeof(cdata->cfg.eeprom_name));
		break;
	case CFG_EEPROM_GET_CAL_DATA:
		CDBG("%s E CFG_EEPROM_GET_CAL_DATA\n", __func__);
		cdata->cfg.get_data.num_bytes =
			e_ctrl->cal_data.num_data;
	    /*+begin lijk3 add eeprom checksum function 2014-07-11*/
	    /*lenovo-sw add for eeprom read only once*/
		if (!strcmp(e_ctrl->eboard_info->eeprom_name, E2PROM_JUCHEN)) {
		    cdata->cfg.get_data.is_3a_checksumed = FRONT_is_3a_checksumed;
		    pr_err("%s E CFG_EEPROM_GET_CAL_DATA FRONT_is_3a_checksumed=%d\n", __func__,cdata->cfg.get_data.is_3a_checksumed);
		} else if (!strcmp(e_ctrl->eboard_info->eeprom_name, E2PROM_ONSEMI)) {
		    cdata->cfg.get_data.is_3a_checksumed = is_3a_checksumed;
		    cdata->cfg.get_data.is_posture_checksumed = is_posture_checksumed;
		    pr_err("%s E CFG_EEPROM_GET_CAL_DATA is_3a_checksumed=%d  is_posture_checksumed=%d\n", __func__,cdata->cfg.get_data.is_3a_checksumed,cdata->cfg.get_data.is_posture_checksumed);
		} else {
		    /*+begin xujt1 add eeprom checksum function 2014-07-11*/
		    cdata->cfg.get_data.is_3a_checksumed = e_ctrl->is_checksumed;
		    /*+end xujt1 add eeprom checksum function 2014-07-11*/
		}
		/*lenovo-sw add end*/
	    /*+end lijk3 add eeprom checksum function 2014-07-11*/

		break;
	case CFG_EEPROM_READ_CAL_DATA:
		CDBG("%s E CFG_EEPROM_READ_CAL_DATA\n", __func__);
		rc = eeprom_config_read_cal_data32(e_ctrl, argp);
		break;
	default:
		break;
	}

	CDBG("%s X rc: %d\n", __func__, rc);
	return rc;
}

static long msm_eeprom_subdev_ioctl32(struct v4l2_subdev *sd,
		unsigned int cmd, void *arg)
{
	struct msm_eeprom_ctrl_t *e_ctrl = v4l2_get_subdevdata(sd);
	void __user *argp = (void __user *)arg;

	CDBG("%s E\n", __func__);
	CDBG("%s:%d a_ctrl %p argp %p\n", __func__, __LINE__, e_ctrl, argp);
	switch (cmd) {
	case VIDIOC_MSM_SENSOR_GET_SUBDEV_ID:
		return msm_eeprom_get_subdev_id(e_ctrl, argp);
	case VIDIOC_MSM_EEPROM_CFG32:
		return msm_eeprom_config32(e_ctrl, argp);
	default:
		return -ENOIOCTLCMD;
	}

	CDBG("%s X\n", __func__);
}

static long msm_eeprom_subdev_do_ioctl32(
	struct file *file, unsigned int cmd, void *arg)
{
	struct video_device *vdev = video_devdata(file);
	struct v4l2_subdev *sd = vdev_to_v4l2_subdev(vdev);

	return msm_eeprom_subdev_ioctl32(sd, cmd, arg);
}

static long msm_eeprom_subdev_fops_ioctl32(struct file *file, unsigned int cmd,
	unsigned long arg)
{
	return video_usercopy(file, cmd, arg, msm_eeprom_subdev_do_ioctl32);
}

#endif

static int msm_eeprom_platform_probe(struct platform_device *pdev)
{
	int rc = 0;
	int j = 0;
	uint32_t temp;
/*+begin lijk3 add eeprom checksum function 2014-07-11*/
	int32_t loop = 0;
	int32_t loopp = 0;

	int32_t data_index;
	int32_t data_3a_sum = 0;
	int32_t data_3a_sum_front;
#ifdef CONFIG_LENOVO_EEPROM_ONSEMI_OIS
	int32_t data_ois_sum;
	int32_t data_posture_sum;
#endif
/*+end lijk3 add eeprom checksum function 2014-07-11*/
	struct msm_camera_cci_client *cci_client = NULL;
	struct msm_eeprom_ctrl_t *e_ctrl = NULL;
	struct msm_eeprom_board_info *eb_info = NULL;
	struct device_node *of_node = pdev->dev.of_node;
	struct msm_camera_power_ctrl_t *power_info = NULL;

	CDBG("%s E\n", __func__);

	e_ctrl = kzalloc(sizeof(*e_ctrl), GFP_KERNEL);
	if (!e_ctrl) {
		pr_err("%s:%d kzalloc failed\n", __func__, __LINE__);
		return -ENOMEM;
	}
	e_ctrl->eeprom_v4l2_subdev_ops = &msm_eeprom_subdev_ops;
	e_ctrl->eeprom_mutex = &msm_eeprom_mutex;

	e_ctrl->is_supported = 0;
	if (!of_node) {
		pr_err("%s dev.of_node NULL\n", __func__);
		return -EINVAL;
	}

	rc = of_property_read_u32(of_node, "cell-index",
		&pdev->id);
	CDBG("cell-index %d, rc %d\n", pdev->id, rc);
	if (rc < 0) {
		pr_err("failed rc %d\n", rc);
		return rc;
	}
	e_ctrl->subdev_id = pdev->id;

	rc = of_property_read_u32(of_node, "qcom,cci-master",
		&e_ctrl->cci_master);
	CDBG("qcom,cci-master %d, rc %d\n", e_ctrl->cci_master, rc);
	if (rc < 0) {
		pr_err("%s failed rc %d\n", __func__, rc);
		return rc;
	}
	rc = of_property_read_u32(of_node, "qcom,slave-addr",
		&temp);
	if (rc < 0) {
		pr_err("%s failed rc %d\n", __func__, rc);
		return rc;
	}

	/* Set platform device handle */
	e_ctrl->pdev = pdev;
	/* Set device type as platform device */
	e_ctrl->eeprom_device_type = MSM_CAMERA_PLATFORM_DEVICE;
	e_ctrl->i2c_client.i2c_func_tbl = &msm_eeprom_cci_func_tbl;
	e_ctrl->i2c_client.cci_client = kzalloc(sizeof(
		struct msm_camera_cci_client), GFP_KERNEL);
	if (!e_ctrl->i2c_client.cci_client) {
		pr_err("%s failed no memory\n", __func__);
		return -ENOMEM;
	}

	e_ctrl->eboard_info = kzalloc(sizeof(
		struct msm_eeprom_board_info), GFP_KERNEL);
	if (!e_ctrl->eboard_info) {
		pr_err("%s failed line %d\n", __func__, __LINE__);
		rc = -ENOMEM;
		goto cciclient_free;
	}
	eb_info = e_ctrl->eboard_info;
	power_info = &eb_info->power_info;
	eb_info->i2c_slaveaddr = temp;

	power_info->clk_info = cam_8974_clk_info;
	power_info->clk_info_size = ARRAY_SIZE(cam_8974_clk_info);
	power_info->dev = &pdev->dev;


	rc = of_property_read_u32(of_node, "qcom,i2c-freq-mode",
		&eb_info->i2c_freq_mode);
	if (rc < 0 || (eb_info->i2c_freq_mode >= I2C_MAX_MODES)) {
		eb_info->i2c_freq_mode = I2C_STANDARD_MODE;
		CDBG("%s Default I2C standard speed mode.\n", __func__);
	}

	CDBG("qcom,slave-addr = 0x%X\n", eb_info->i2c_slaveaddr);
	cci_client = e_ctrl->i2c_client.cci_client;
	cci_client->cci_subdev = msm_cci_get_subdev();
	cci_client->cci_i2c_master = e_ctrl->cci_master;
	cci_client->sid = eb_info->i2c_slaveaddr >> 1;
	cci_client->retries = 3;
	cci_client->id_map = 0;
	cci_client->i2c_freq_mode = eb_info->i2c_freq_mode;

	rc = of_property_read_string(of_node, "qcom,eeprom-name",
		&eb_info->eeprom_name);
	CDBG("%s qcom,eeprom-name %s, rc %d\n", __func__,
		eb_info->eeprom_name, rc);
	if (rc < 0) {
		pr_err("%s failed %d\n", __func__, __LINE__);
		goto board_free;
	}

	rc = msm_eeprom_cmm_dts(e_ctrl->eboard_info, of_node);
	if (rc < 0)
		CDBG("%s MM data miss:%d\n", __func__, __LINE__);

	rc = msm_eeprom_get_dt_data(e_ctrl);
	if (rc)
		goto board_free;

	rc = msm_eeprom_parse_memory_map(of_node, &e_ctrl->cal_data);
	if (rc < 0)
		goto board_free;

	rc = msm_camera_power_up(power_info, e_ctrl->eeprom_device_type,
		&e_ctrl->i2c_client);
	if (rc) {
		pr_err("failed rc %d\n", rc);
		goto memdata_free;
	}

	/* begin: add ljk for checksum eeprom*/
	if (!strcmp(eb_info->eeprom_name, E2PROM_JUCHEN))
	{
        do{
            data_3a_sum_front = 0;
    	    rc = read_eeprom_memory(e_ctrl, &e_ctrl->cal_data);
    	    if (rc < 0) {
        		pr_err("%s read_eeprom_memory failed loop=%d\n", __func__,loop);
        	}
        	else if((rc < 0)&&(loop>4))
        	{
        		pr_err("%s read_eeprom_memory failed loop=%d\n", __func__,loop);
    		goto power_down;
    	    }
        	else
        	{
            	    pr_err("%s: module version = %d \n",__func__,e_ctrl->cal_data.mapdata[FRONT_THREEA_BEGIN_OFFSET+6]);

                    for(loopp=18;loopp<25;loopp++)
                    {
            	                pr_err(" %d ",e_ctrl->cal_data.mapdata[loopp]);
                    }
            	    pr_err("%s: year = %d \n",__func__,e_ctrl->cal_data.mapdata[FRONT_THREEA_BEGIN_OFFSET+26]);
            	    pr_err("%s: mount = %d \n",__func__,e_ctrl->cal_data.mapdata[FRONT_THREEA_BEGIN_OFFSET+27]);
            	    pr_err("%s: day = %d \n",__func__,e_ctrl->cal_data.mapdata[FRONT_THREEA_BEGIN_OFFSET+28]);
            	    pr_err("%s: hour = %d \n",__func__,e_ctrl->cal_data.mapdata[FRONT_THREEA_BEGIN_OFFSET+29]);
            	    pr_err("%s: min = %d \n",__func__,e_ctrl->cal_data.mapdata[FRONT_THREEA_BEGIN_OFFSET+30]);
            	    pr_err("%s: second = %d \n",__func__,e_ctrl->cal_data.mapdata[FRONT_THREEA_BEGIN_OFFSET+31]);

            	for(data_index=FRONT_THREEA_BEGIN_OFFSET; data_index <= FRONT_THREEA_END_OFFSET; data_index++)
            	{
                    data_3a_sum_front = data_3a_sum_front+e_ctrl->cal_data.mapdata[data_index];
        		    CDBG("%s data_3a_sum_front=0x%x .mapdata[%d]=0x%x\n", __func__,data_3a_sum_front,data_index,e_ctrl->cal_data.mapdata[data_index]);

            	}
            	    pr_err("%s: data_3a_sum_front = 0x%x  temp_data=0x%x 0x%x\n",__func__,data_3a_sum_front,(*(e_ctrl->cal_data.mapdata+FRONT_THREEA_CHKSUM_HI_OFFSET)),(*(e_ctrl->cal_data.mapdata+FRONT_THREEA_CHKSUM_LOW_OFFSET)));
                    if((data_3a_sum_front&0xffff)==(((*(e_ctrl->cal_data.mapdata+FRONT_THREEA_CHKSUM_HI_OFFSET))<<8)|(*(e_ctrl->cal_data.mapdata+FRONT_THREEA_CHKSUM_LOW_OFFSET))))
                    {
                        pr_err("%s: data_3a_sum_front data success!  loop = %d\n",__func__,loop);
                        FRONT_is_3a_checksumed = 1;
                    }
                    else //checksum==0
                    {
                        pr_err("%s: data_3a_sum_front data fail!  loop=%d\n",__func__,loop);
                        FRONT_is_3a_checksumed = 0;
                    }
            }
            loop++;
        } while ((loop < 3)&&(FRONT_is_3a_checksumed == 0));

    	for (j = 0; j < e_ctrl->cal_data.num_data; j++)
    		CDBG("front_memory_data[%d] = 0x%X\n", j, e_ctrl->cal_data.mapdata[j]);
        eeprom_front_data_ctrl = e_ctrl;
		
		if (loop >= 3) {
			pr_err("%s loop=%d >= 3 read_eeprom_memory failed\n", __func__,loop);
			goto power_down;
		}		
	}
#ifdef CONFIG_LENOVO_EEPROM_ONSEMI_OIS
	//rear camera eeprom onsemi
	else if (!strcmp(eb_info->eeprom_name, E2PROM_ONSEMI))
	{
        do{
            data_3a_sum = 0;
            data_ois_sum= 0;
            data_posture_sum = 0;
    	rc = read_eeprom_memory(e_ctrl, &e_ctrl->cal_data);
    	if (rc < 0) {
        		pr_err("%s read_eeprom_memory failed loop=%d\n", __func__,loop);
        	}
        	else if((rc < 0)&&(loop>4))
        	{
        		pr_err("%s read_eeprom_memory failed loop=%d\n", __func__,loop);
    		goto power_down;
    	}
        	else
        	{
            	for(data_index=THREEA_BEGIN_OFFSET; data_index <= THREEA_END_OFFSET; data_index++)
            	{
                    data_3a_sum = data_3a_sum+e_ctrl->cal_data.mapdata[data_index];
            	}
            	    pr_err("%s: data_3a_sum = 0x%x  temp_data=0x%x 0x%x\n",__func__,data_3a_sum,(*(e_ctrl->cal_data.mapdata+THREEA_CHKSUM_HI_OFFSET)),(*(e_ctrl->cal_data.mapdata+THREEA_CHKSUM_LOW_OFFSET)));
                    if((data_3a_sum&0xffff)==(((*(e_ctrl->cal_data.mapdata+THREEA_CHKSUM_HI_OFFSET))<<8)|(*(e_ctrl->cal_data.mapdata+THREEA_CHKSUM_LOW_OFFSET))))
                    {
                        pr_err("%s: data_3a_sum data success!  loop = %d\n",__func__,loop);
                        is_3a_checksumed = 1;
                    }
                    else //checksum==0
                    {
                        pr_err("%s: data_3a_sum data fail!  loop=%d\n",__func__,loop);
                        is_3a_checksumed = 0;
                    }
            	for(data_index=OIS_BEGIN_OFFSET; data_index <= OIS_END_OFFSET; data_index++)
            	{
                    data_ois_sum = data_ois_sum+e_ctrl->cal_data.mapdata[data_index];
            	}
            	    pr_err("%s: data_ois_sum = 0x%x  temp_data=0x%x 0x%x\n",__func__,data_ois_sum,(*(e_ctrl->cal_data.mapdata+OIS_CHKSUM_HI_OFFSET)),(*(e_ctrl->cal_data.mapdata+OIS_CHKSUM_LOW_OFFSET)));
                    if((data_ois_sum&0xffff)==(((*(e_ctrl->cal_data.mapdata+OIS_CHKSUM_HI_OFFSET))<<8)|(*(e_ctrl->cal_data.mapdata+OIS_CHKSUM_LOW_OFFSET))))
                    {
                        pr_err("%s: data_ois_sum data success!  loop = %d\n",__func__,loop);
                        is_ois_checksumed = 1;
                    }
                    else //checksum==0
                    {
                        pr_err("%s: data_ois_sum data fail!  loop=%d\n",__func__,loop);
                        is_ois_checksumed = 0;
                    }

            	for(data_index=POSTURE_BEGIN_OFFSET; data_index <= POSTURE_END_OFFSET; data_index++)
            	{
                    data_posture_sum = data_posture_sum+e_ctrl->cal_data.mapdata[data_index];
                    //pr_err("memory_data[%d] = 0x%X\n", data_index,e_ctrl->cal_data.mapdata[data_index]);
            	}
            	    pr_err("%s: data_posture_sum = 0x%x  temp_data=0x%x 0x%x\n",__func__,data_posture_sum,(*(e_ctrl->cal_data.mapdata+POSTURE_CHKSUM_HI_OFFSET)),(*(e_ctrl->cal_data.mapdata+POSTURE_CHKSUM_LOW_OFFSET)));
                    if((data_posture_sum&0xffff)==(((*(e_ctrl->cal_data.mapdata+POSTURE_CHKSUM_HI_OFFSET))<<8)|(*(e_ctrl->cal_data.mapdata+POSTURE_CHKSUM_LOW_OFFSET))))
                    {
                        pr_err("%s: data_posture_sum data success!  loop = %d\n",__func__,loop);
                        is_posture_checksumed = 1;
                    }
                    else //checksum==0
                    {
                        pr_err("%s: data_posture_sum data fail!  loop=%d\n",__func__,loop);
                        is_posture_checksumed = 0;
                    }

            }
            loop++;
        } while ((loop < 3)&&((is_3a_checksumed == 0) || (is_ois_checksumed == 0) ));  //||(is_posture_checksumed == 0)

		for (j = 0; j < e_ctrl->cal_data.num_data; j++)
			CDBG("memory_data[%d] = 0x%X\n", j,	e_ctrl->cal_data.mapdata[j]);

        CDBG("%s line %d\n", __func__, __LINE__);
        eeprom_rear_data_ctrl = e_ctrl;
    	E2pDat_Lenovo(eeprom_rear_data_ctrl->cal_data.mapdata);
        pr_err("%s line %d memory_data=%p\n", __func__, __LINE__,eeprom_rear_data_ctrl->cal_data.mapdata);
		
		if (loop >= 3) {
			pr_err("%s loop=%d >= 3 read_eeprom_memory failed\n", __func__,loop);
			goto power_down;
		}
    }
#endif
	/* end: add ljk for checksum eeprom*/
/*+Begin: chenglong1 add for */
    else if (!strcmp(eb_info->eeprom_name, E2PROM_SUNNY_F13S01M)) {
	do {
		rc = read_eeprom_memory(e_ctrl, &e_ctrl->cal_data);
		if (rc < 0) {
			pr_err("%s read_eeprom_memory failed loop=%d\n", __func__,loop);
		}
		else if((rc < 0)&&(loop>4))
		{
			pr_err("%s read_eeprom_memory failed loop=%d\n", __func__,loop);
			goto power_down;
		} else {
			uint16_t checksum = 0;
			uint16_t calced_checksum = 0;

			checksum = sunny_f13s01m_dw9761_calc_checksum(&(e_ctrl->cal_data.mapdata[SUNNY_F13S01M_MODULE_INFO_OFFSET]), SUNNY_F13S01M_MODULE_INFO_SIZE);
			calced_checksum = ((uint16_t)e_ctrl->cal_data.mapdata[SUNNY_F13S01M_MODULE_INFO_OFFSET+SUNNY_F13S01M_MODULE_INFO_SIZE] <<8) |
			(uint16_t)e_ctrl->cal_data.mapdata[SUNNY_F13S01M_MODULE_INFO_OFFSET+SUNNY_F13S01M_MODULE_INFO_SIZE+1];
			pr_err("%s sunny_f13s01m_dw9761b module info checksum: %x, checksum readback: %x\n", __func__, checksum,  calced_checksum);
			module_info_checksum_pass = (checksum == calced_checksum) ? 1 : 0;

			checksum = sunny_f13s01m_dw9761_calc_checksum(&(e_ctrl->cal_data.mapdata[SUNNY_F13S01M_AFC_OFFSET]), SUNNY_F13S01M_AFC_SIZE);
			calced_checksum = ((uint16_t)e_ctrl->cal_data.mapdata[SUNNY_F13S01M_AFC_OFFSET+SUNNY_F13S01M_AFC_SIZE] <<8) |
			(uint16_t)e_ctrl->cal_data.mapdata[SUNNY_F13S01M_AFC_OFFSET+SUNNY_F13S01M_AFC_SIZE+1];
			pr_err("%s sunny_f13s01m_dw9761b afc checksum: %x, checksum readback: %x\n", __func__, checksum,  calced_checksum);
			afc_checksum_pass = (checksum == calced_checksum) ? 1 : 0;

			checksum = sunny_f13s01m_dw9761_calc_checksum(&(e_ctrl->cal_data.mapdata[SUNNY_F13S01M_WBC_OFFSET]), SUNNY_F13S01M_WBC_SIZE);
			calced_checksum = ((uint16_t)e_ctrl->cal_data.mapdata[SUNNY_F13S01M_WBC_OFFSET+SUNNY_F13S01M_WBC_SIZE] <<8) |
			(uint16_t)e_ctrl->cal_data.mapdata[SUNNY_F13S01M_WBC_OFFSET+SUNNY_F13S01M_WBC_SIZE+1];
			pr_err("%s sunny_f13s01m_dw9761b wbc checksum: %x, checksum readback: %x\n", __func__, checksum,  calced_checksum);
			wbc_checksum_pass = (checksum == calced_checksum) ? 1 : 0;

			checksum = sunny_f13s01m_dw9761_calc_checksum(&(e_ctrl->cal_data.mapdata[SUNNY_F13S01M_LSC_OFFSET]), SUNNY_F13S01M_LSC_SIZE);
			calced_checksum = ((uint16_t)e_ctrl->cal_data.mapdata[SUNNY_F13S01M_LSC_OFFSET+SUNNY_F13S01M_LSC_SIZE] <<8) |
			(uint16_t)e_ctrl->cal_data.mapdata[SUNNY_F13S01M_LSC_OFFSET+SUNNY_F13S01M_LSC_SIZE+1];
			pr_err("%s sunny_f13s01m_dw9761b lsc checksum: %x, checksum readback: %x\n", __func__, checksum,  calced_checksum);
			lsc_checksum_pass = (checksum == calced_checksum) ? 1 : 0;

			checksum = sunny_f13s01m_dw9761_calc_checksum(&(e_ctrl->cal_data.mapdata[SUNNY_F13S01M_PDAF_GM_OFFSET]), SUNNY_F13S01M_PDAF_GM_SIZE);
			calced_checksum = ((uint16_t)e_ctrl->cal_data.mapdata[SUNNY_F13S01M_PDAF_GM_OFFSET+SUNNY_F13S01M_PDAF_GM_SIZE] <<8) |
			(uint16_t)e_ctrl->cal_data.mapdata[SUNNY_F13S01M_PDAF_GM_OFFSET+SUNNY_F13S01M_PDAF_GM_SIZE+1];
			pr_err("%s sunny_f13s01m_dw9761b pdaf gain map checksum: %x, checksum readback: %x\n", __func__, checksum,  calced_checksum);
			pdaf_gain_map_checksum_pass = (checksum == calced_checksum) ? 1 : 0;

			checksum = sunny_f13s01m_dw9761_calc_checksum(&(e_ctrl->cal_data.mapdata[SUNNY_F13S01M_PDAF_COEF_OFFSET]), SUNNY_F13S01M_PDAF_COEF_SIZE);
			calced_checksum = ((uint16_t)e_ctrl->cal_data.mapdata[SUNNY_F13S01M_PDAF_COEF_OFFSET+SUNNY_F13S01M_PDAF_COEF_SIZE] <<8) |
			(uint16_t)e_ctrl->cal_data.mapdata[SUNNY_F13S01M_PDAF_COEF_OFFSET+SUNNY_F13S01M_PDAF_COEF_SIZE+1];
			pr_err("%s sunny_f13s01m_dw9761b pdaf conversion coef checksum: %x, checksum readback: %x\n", __func__, checksum,  calced_checksum);
			pdaf_conv_coef_checksum_pass = (checksum == calced_checksum) ? 1 : 0;
		}

		loop++;
	}while (loop<3 && (!module_info_checksum_pass || !afc_checksum_pass || 
	                                                       !wbc_checksum_pass || !lsc_checksum_pass || 
	                                                       !pdaf_gain_map_checksum_pass || !pdaf_conv_coef_checksum_pass));
	if (loop < 3) {
		e_ctrl->is_checksumed = 1;
	} else {
		e_ctrl->is_checksumed = 0;
	}
    }
/*+End.*/
    else {
		rc = read_eeprom_memory(e_ctrl, &e_ctrl->cal_data);
		if (rc < 0) {
			pr_err("%s read_eeprom_memory failed\n", __func__);
			goto power_down;
		}
	
		for (j = 0; j < e_ctrl->cal_data.num_data; j++)
			CDBG("memory_data[%d] = 0x%X\n", j,	e_ctrl->cal_data.mapdata[j]);

	/*+begin xujt1 add eeprom checksum function 2014-07-11*/
		for (j = THREEA_BEGIN_OFFSET; j <= THREEA_END_OFFSET; j++)
		{
			data_3a_sum = data_3a_sum + e_ctrl->cal_data.mapdata[j];
		}
		pr_err("eeprom data_3a_sum = 0x%x\n", data_3a_sum);
	
		e_ctrl->is_checksumed = 0;
		if ((data_3a_sum & 0xffff)==(((*(e_ctrl->cal_data.mapdata+THREEA_CHKSUM_HI_OFFSET))<<8)|(*(e_ctrl->cal_data.mapdata+THREEA_CHKSUM_LOW_OFFSET))))
		{
			e_ctrl->is_checksumed = 1;
		}
		pr_err("eeprom is_checksumed = %d\n", e_ctrl->is_checksumed);	
	/*+end xujt1 add eeprom checksum function 2014-07-11*/
    }

	e_ctrl->is_supported |= msm_eeprom_match_crc(&e_ctrl->cal_data);

	rc = msm_camera_power_down(power_info, e_ctrl->eeprom_device_type,
		&e_ctrl->i2c_client);
	if (rc) {
		pr_err("failed rc %d\n", rc);
		goto memdata_free;
	}
	v4l2_subdev_init(&e_ctrl->msm_sd.sd,
		e_ctrl->eeprom_v4l2_subdev_ops);
	v4l2_set_subdevdata(&e_ctrl->msm_sd.sd, e_ctrl);
	platform_set_drvdata(pdev, &e_ctrl->msm_sd.sd);
	e_ctrl->msm_sd.sd.internal_ops = &msm_eeprom_internal_ops;
	e_ctrl->msm_sd.sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	snprintf(e_ctrl->msm_sd.sd.name,
		ARRAY_SIZE(e_ctrl->msm_sd.sd.name), "msm_eeprom");
	media_entity_init(&e_ctrl->msm_sd.sd.entity, 0, NULL, 0);
	e_ctrl->msm_sd.sd.entity.type = MEDIA_ENT_T_V4L2_SUBDEV;
	e_ctrl->msm_sd.sd.entity.group_id = MSM_CAMERA_SUBDEV_EEPROM;
	msm_sd_register(&e_ctrl->msm_sd);

#ifdef CONFIG_COMPAT
	msm_eeprom_v4l2_subdev_fops = v4l2_subdev_fops;
	msm_eeprom_v4l2_subdev_fops.compat_ioctl32 =
		msm_eeprom_subdev_fops_ioctl32;
	e_ctrl->msm_sd.sd.devnode->fops = &msm_eeprom_v4l2_subdev_fops;
#endif

	e_ctrl->is_supported = (e_ctrl->is_supported << 1) | 1;
	CDBG("%s X\n", __func__);
	return rc;

power_down:
	msm_camera_power_down(power_info, e_ctrl->eeprom_device_type,
		&e_ctrl->i2c_client);
memdata_free:
	kfree(e_ctrl->cal_data.mapdata);
	kfree(e_ctrl->cal_data.map);
board_free:
	kfree(e_ctrl->eboard_info);
cciclient_free:
	kfree(e_ctrl->i2c_client.cci_client);
	kfree(e_ctrl);
	return rc;
}

static int msm_eeprom_platform_remove(struct platform_device *pdev)
{
	struct v4l2_subdev *sd = platform_get_drvdata(pdev);
	struct msm_eeprom_ctrl_t  *e_ctrl;
	if (!sd) {
		pr_err("%s: Subdevice is NULL\n", __func__);
		return 0;
	}

	e_ctrl = (struct msm_eeprom_ctrl_t *)v4l2_get_subdevdata(sd);
	if (!e_ctrl) {
		pr_err("%s: eeprom device is NULL\n", __func__);
		return 0;
	}

	kfree(e_ctrl->i2c_client.cci_client);
	kfree(e_ctrl->cal_data.mapdata);
	kfree(e_ctrl->cal_data.map);
	if (e_ctrl->eboard_info) {
		kfree(e_ctrl->eboard_info->power_info.gpio_conf);
		kfree(e_ctrl->eboard_info);
	}
	kfree(e_ctrl);
	return 0;
}

static const struct of_device_id msm_eeprom_dt_match[] = {
	{ .compatible = "qcom,eeprom" },
	{ }
};

MODULE_DEVICE_TABLE(of, msm_eeprom_dt_match);

static struct platform_driver msm_eeprom_platform_driver = {
	.driver = {
		.name = "qcom,eeprom",
		.owner = THIS_MODULE,
		.of_match_table = msm_eeprom_dt_match,
	},
	.remove = msm_eeprom_platform_remove,
};

static const struct i2c_device_id msm_eeprom_i2c_id[] = {
	{ "msm_eeprom", (kernel_ulong_t)NULL},
	{ }
};

static struct i2c_driver msm_eeprom_i2c_driver = {
	.id_table = msm_eeprom_i2c_id,
	.probe  = msm_eeprom_i2c_probe,
	.remove = msm_eeprom_i2c_remove,
	.driver = {
		.name = "msm_eeprom",
	},
};

static struct spi_driver msm_eeprom_spi_driver = {
	.driver = {
		.name = "qcom_eeprom",
		.owner = THIS_MODULE,
		.of_match_table = msm_eeprom_dt_match,
	},
	.probe = msm_eeprom_spi_probe,
	.remove = msm_eeprom_spi_remove,
};

static int __init msm_eeprom_init_module(void)
{
	int rc = 0;
	CDBG("%s E\n", __func__);
	rc = platform_driver_probe(&msm_eeprom_platform_driver,
		msm_eeprom_platform_probe);
	CDBG("%s:%d platform rc %d\n", __func__, __LINE__, rc);
	rc = spi_register_driver(&msm_eeprom_spi_driver);
	CDBG("%s:%d spi rc %d\n", __func__, __LINE__, rc);
	return i2c_add_driver(&msm_eeprom_i2c_driver);
}

static void __exit msm_eeprom_exit_module(void)
{
	platform_driver_unregister(&msm_eeprom_platform_driver);
	spi_unregister_driver(&msm_eeprom_spi_driver);
	i2c_del_driver(&msm_eeprom_i2c_driver);
}

module_init(msm_eeprom_init_module);
module_exit(msm_eeprom_exit_module);
MODULE_DESCRIPTION("MSM EEPROM driver");
MODULE_LICENSE("GPL v2");
