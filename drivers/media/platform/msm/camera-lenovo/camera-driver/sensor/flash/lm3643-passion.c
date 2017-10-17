/* Copyright (c) 2014, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#include <linux/module.h>
#include <linux/export.h>
#include "msm_camera_io_util.h"
#include "msm_led_flash.h"

#define FLASH_NAME "ti,lm3642"

#define CONFIG_MSMB_CAMERA_DEBUG
#ifdef CONFIG_MSMB_CAMERA_DEBUG
#define LM3642_DBG(fmt, args...) pr_err(fmt, ##args)
#else
#define LM3642_DBG(fmt, args...)
#endif


static struct msm_led_flash_ctrl_t fctrl;
static struct i2c_driver lm3642_i2c_driver;

static struct msm_camera_i2c_reg_array lm3642_init_array[] = {
	{0x01, 0xf0},
	//{0x07, 0x88},
	{0x08, 0x0f},
};

static struct msm_camera_i2c_reg_array lm3642_off_array[] = {
	{0x01, 0xf0},
};

static struct msm_camera_i2c_reg_array lm3642_release_array[] = {
	{0x01, 0xf0},
};

#define FLASH_LOW_LED1_INDEX 0
#define FLASH_LOW_LED2_INDEX 1
#define FLASH_LOW_CTRL_INDEX 2

#define FLASH_HIGH_LED1_INDEX 0
#define FLASH_HIGH_LED2_INDEX 1
#define FLASH_HIGH_DURATION_INDEX 2
#define FLASH_HIGH_CTRL_INDEX 3

static struct msm_camera_i2c_reg_array lm3642_low_array[] = {
	{0x05, 0x3A},
	{0x06, 0x3A},
	{0x01, 0xDB},
};

static struct msm_camera_i2c_reg_array lm3642_high_array[] = {
	{0x03, 0x43},
	{0x04, 0x43},
	{0x08, 0x0f},
	{0x01, 0xDF},
};


static const struct of_device_id lm3642_i2c_trigger_dt_match[] = {
	{.compatible = "ti,lm3642"},
	{}
};

MODULE_DEVICE_TABLE(of, lm3642_i2c_trigger_dt_match);
static const struct i2c_device_id lm3642_i2c_id[] = {
	{FLASH_NAME, (kernel_ulong_t)&fctrl},
	{ }
};

static void msm_led_torch_brightness_set(struct led_classdev *led_cdev,
				enum led_brightness value)
{
	if (value > LED_OFF) {
		if(fctrl.func_tbl->flash_led_low)
			fctrl.func_tbl->flash_led_low(&fctrl);
	} else {
		if(fctrl.func_tbl->flash_led_off)
			fctrl.func_tbl->flash_led_off(&fctrl);
	}
};

static struct led_classdev msm_torch_led = {
	.name			= "torch-light",
	.brightness_set	= msm_led_torch_brightness_set,
	.brightness		= LED_OFF,
};

static int32_t msm_lm3642_torch_create_classdev(struct device *dev ,
				void *data)
{
	int rc;
	msm_led_torch_brightness_set(&msm_torch_led, LED_OFF);
	rc = led_classdev_register(dev, &msm_torch_led);
	if (rc) {
		pr_err("Failed to register led dev. rc = %d\n", rc);
		return rc;
	}

	return 0;
};

int msm_flash_lm3642_led_init(struct msm_led_flash_ctrl_t *fctrl)
{
	int rc = 0;
	struct msm_camera_sensor_board_info *flashdata = NULL;
	struct msm_camera_power_ctrl_t *power_info = NULL;
	LM3642_DBG("%s:%d called\n", __func__, __LINE__);

	flashdata = fctrl->flashdata;
	power_info = &flashdata->power_info;

	gpio_set_value_cansleep(
		power_info->gpio_conf->gpio_num_info->
		gpio_num[SENSOR_GPIO_FL_NOW],
		GPIO_OUT_LOW);

	if (fctrl->flash_i2c_client && fctrl->reg_setting) {
		rc = fctrl->flash_i2c_client->i2c_func_tbl->i2c_write_table(
			fctrl->flash_i2c_client,
			fctrl->reg_setting->init_setting);
		if (rc < 0)
			pr_err("%s:%d failed\n", __func__, __LINE__);
	}
	return rc;
}

int msm_flash_lm3642_led_release(struct msm_led_flash_ctrl_t *fctrl)
{
	int rc = 0;
	struct msm_camera_sensor_board_info *flashdata = NULL;
	struct msm_camera_power_ctrl_t *power_info = NULL;

	flashdata = fctrl->flashdata;
	power_info = &flashdata->power_info;
	LM3642_DBG("%s:%d called\n", __func__, __LINE__);
	if (!fctrl) {
		pr_err("%s:%d fctrl NULL\n", __func__, __LINE__);
		return -EINVAL;
	}

	gpio_set_value_cansleep(
		power_info->gpio_conf->gpio_num_info->
		gpio_num[SENSOR_GPIO_FL_NOW],
		GPIO_OUT_LOW);
	if (fctrl->flash_i2c_client && fctrl->reg_setting) {
		rc = fctrl->flash_i2c_client->i2c_func_tbl->i2c_write_table(
			fctrl->flash_i2c_client,
			fctrl->reg_setting->release_setting);
		if (rc < 0)
			pr_err("%s:%d failed\n", __func__, __LINE__);
	}
	return 0;
}

int msm_flash_lm3642_led_off(struct msm_led_flash_ctrl_t *fctrl)
{
	int rc = 0;
	struct msm_camera_sensor_board_info *flashdata = NULL;
	struct msm_camera_power_ctrl_t *power_info = NULL;

	flashdata = fctrl->flashdata;
	power_info = &flashdata->power_info;
	LM3642_DBG("%s:%d called\n", __func__, __LINE__);

	if (!fctrl) {
		pr_err("%s:%d fctrl NULL\n", __func__, __LINE__);
		return -EINVAL;
	}

	if (fctrl->flash_i2c_client && fctrl->reg_setting) {
		rc = fctrl->flash_i2c_client->i2c_func_tbl->i2c_write_table(
			fctrl->flash_i2c_client,
			fctrl->reg_setting->off_setting);
		if (rc < 0)
			pr_err("%s:%d failed\n", __func__, __LINE__);
	}

	gpio_set_value_cansleep(
		power_info->gpio_conf->gpio_num_info->
		gpio_num[SENSOR_GPIO_FL_NOW],
		GPIO_OUT_LOW);

	return rc;
}

int msm_flash_lm3642_led_low(struct msm_led_flash_ctrl_t *fctrl)
{
	int rc = 0;
	struct msm_camera_sensor_board_info *flashdata = NULL;
	struct msm_camera_power_ctrl_t *power_info = NULL;
	LM3642_DBG("%s:%d called\n", __func__, __LINE__);

	flashdata = fctrl->flashdata;
	power_info = &flashdata->power_info;


	gpio_set_value_cansleep(
		power_info->gpio_conf->gpio_num_info->
		gpio_num[SENSOR_GPIO_FL_NOW],
		GPIO_OUT_HIGH);

	if (fctrl->flash_i2c_client && fctrl->reg_setting) {
		rc = fctrl->flash_i2c_client->i2c_func_tbl->i2c_write_table(
			fctrl->flash_i2c_client,
			fctrl->reg_setting->low_setting);
		if (rc < 0)
			pr_err("%s:%d failed\n", __func__, __LINE__);
	}

	return rc;
}

int msm_flash_lm3642_led_high(struct msm_led_flash_ctrl_t *fctrl)
{
	int rc = 0;
	struct msm_camera_sensor_board_info *flashdata = NULL;
	struct msm_camera_power_ctrl_t *power_info = NULL;
	LM3642_DBG("%s:%d called\n", __func__, __LINE__);

	flashdata = fctrl->flashdata;

	power_info = &flashdata->power_info;

	gpio_set_value_cansleep(
		power_info->gpio_conf->gpio_num_info->
		gpio_num[SENSOR_GPIO_FL_NOW],
		GPIO_OUT_HIGH);

	if (fctrl->flash_i2c_client && fctrl->reg_setting) {
		rc = fctrl->flash_i2c_client->i2c_func_tbl->i2c_write_table(
			fctrl->flash_i2c_client,
			fctrl->reg_setting->high_setting);
		if (rc < 0)
			pr_err("%s:%d failed\n", __func__, __LINE__);
	}

	return rc;
}
static int msm_flash_lm3642_i2c_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{
	struct msm_camera_sensor_board_info *flashdata = NULL;
	struct msm_camera_power_ctrl_t *power_info = NULL;
	int rc = 0 ;
	LM3642_DBG("%s entry\n", __func__);
	if (!id) {
		pr_err("msm_flash_lm3642_i2c_probe: id is NULL");
		id = lm3642_i2c_id;
	}
	rc = msm_flash_i2c_probe(client, id);

	flashdata = fctrl.flashdata;
	power_info = &flashdata->power_info;

	rc = msm_camera_request_gpio_table(
		power_info->gpio_conf->cam_gpio_req_tbl,
		power_info->gpio_conf->cam_gpio_req_tbl_size, 1);
	if (rc < 0) {
		pr_err("%s: request gpio failed\n", __func__);
		return rc;
	}

	if (fctrl.pinctrl_info.use_pinctrl == true) {
		pr_err("%s:%d PC:: flash pins setting to active state",
				__func__, __LINE__);
		rc = pinctrl_select_state(fctrl.pinctrl_info.pinctrl,
				fctrl.pinctrl_info.gpio_state_active);
		if (rc)
			pr_err("%s:%d cannot set pin to active state",
					__func__, __LINE__);
	}

	if (!rc)
		msm_lm3642_torch_create_classdev(&(client->dev),NULL);
	return rc;
}

static int msm_flash_lm3642_i2c_remove(struct i2c_client *client)
{
	struct msm_camera_sensor_board_info *flashdata = NULL;
	struct msm_camera_power_ctrl_t *power_info = NULL;
	int rc = 0 ;
	LM3642_DBG("%s entry\n", __func__);
	flashdata = fctrl.flashdata;
	power_info = &flashdata->power_info;

	rc = msm_camera_request_gpio_table(
		power_info->gpio_conf->cam_gpio_req_tbl,
		power_info->gpio_conf->cam_gpio_req_tbl_size, 0);
	if (rc < 0) {
		pr_err("%s: request gpio failed\n", __func__);
		return rc;
	}

	if (fctrl.pinctrl_info.use_pinctrl == true) {
		rc = pinctrl_select_state(fctrl.pinctrl_info.pinctrl,
				fctrl.pinctrl_info.gpio_state_suspend);
		if (rc)
			pr_err("%s:%d cannot set pin to suspend state",
				__func__, __LINE__);
	}
	return rc;
}


static struct i2c_driver lm3642_i2c_driver = {
	.id_table = lm3642_i2c_id,
	.probe  = msm_flash_lm3642_i2c_probe,
	.remove = msm_flash_lm3642_i2c_remove,
	.driver = {
		.name = FLASH_NAME,
		.owner = THIS_MODULE,
		.of_match_table = lm3642_i2c_trigger_dt_match,
	},
};

/*lenovo-sw chenglong1 add for flash drv*/
static const struct of_device_id lm3642_trigger_dt_match[] = {
	{.compatible = "ti,lm3642", .data = &fctrl},
	{}
};

static int msm_flash_lm3642_platform_probe(struct platform_device *pdev)
{
	const struct of_device_id *match;
	match = of_match_device(lm3642_trigger_dt_match, &pdev->dev);
	if (!match)
		return -EFAULT;
	return msm_flash_probe(pdev, match->data);
}


static struct platform_driver lm3642_platform_driver = {
	.probe = msm_flash_lm3642_platform_probe,
	.driver = {
		.name = "ti,lm3642",
		.owner = THIS_MODULE,
		.of_match_table = lm3642_trigger_dt_match,
	},
};
/*lenovo-sw add end*/

static int __init msm_flash_lm3642_init(void)
{
	/*lenovo-sw chenglong1 add for flash drv*/
	int32_t rc = 0;
	rc = platform_driver_register(&lm3642_platform_driver);
	LM3642_DBG("%s:%d rc %d   1\n", __func__, __LINE__, rc);
	if (!rc)
		return rc;	
	/*lenovo-sw modify end*/
	LM3642_DBG("%s entry\n", __func__);
	return i2c_add_driver(&lm3642_i2c_driver);
}

static void __exit msm_flash_lm3642_exit(void)
{
	LM3642_DBG("%s entry\n", __func__);
	i2c_del_driver(&lm3642_i2c_driver);
	return;
}

/*+Begin: chenglong1 add for flash current multi-level 2015-4-5*/

/*
** 
** Flash Current formula: LED1/LED2
** mA = (Brightness Code x 11.725) + 10.9mA
**
*/
int lm3642_flash_current_to_reg(uint32_t current_in_ma, uint32_t led_idx)
{

	uint8_t flash_level = (current_in_ma >= 11) ? ((current_in_ma*1000-10900)/11725)  :  0;

	if (led_idx > 1) {
		LM3642_DBG("%s: led index: %d exceed limitaion!!!\n", __func__, led_idx);
		return -1;
	}
	if (flash_level >127) {
		LM3642_DBG("%s: flash level: %d exceed current limitaion!!!\n", __func__, flash_level);
		//return -1;
		flash_level = 127;
	}
	
	if (flash_level) {
		lm3642_high_array[FLASH_HIGH_CTRL_INDEX].reg_data |= (0x01<<led_idx);
		lm3642_high_array[FLASH_HIGH_LED1_INDEX+led_idx].reg_data = flash_level;
	} else {
		lm3642_high_array[FLASH_HIGH_CTRL_INDEX].reg_data &= ~(0x01<<led_idx);
	}
	LM3642_DBG("%s: prepare flash level: %d success\n", __func__, flash_level);
	//LM3642_DBG("%s:lm3642_low_array[0~3]: %x, %x, %x, %x\n", __func__, lm3642_high_array[0].reg_data, lm3642_high_array[1].reg_data, lm3642_high_array[2].reg_data, lm3642_high_array[3].reg_data);
	
	return 0;
}

/*
** 
** Torch Current formula: LED1/LED2
** mA = (Brightness Code x 1.4mA) + 0.977mA
**
*/
int lm3642_torch_current_to_reg(uint32_t current_in_ma, uint32_t led_idx)
{
	uint8_t torch_level =  (current_in_ma >= 1) ? ((current_in_ma*1000-977)/1400)  :  0;

	if (led_idx > 1) {
		LM3642_DBG("%s: led index: %d exceed limitaion!!!\n", __func__, led_idx);
		return -1;
	}

	//current set as 0 only in torch-light sysf node
	if (!current_in_ma) {
		LM3642_DBG("%s: led index: %d current 0 using default\n", __func__, led_idx);
		torch_level = (led_idx == 0) ? 75 : 10;
	}
	
	if (torch_level >127) {
		LM3642_DBG("%s: torch_level: %d exceed current limitaion!!!\n", __func__, torch_level);
		//return -1;
		torch_level = 127;
	}

	if (torch_level) {
		lm3642_low_array[FLASH_LOW_CTRL_INDEX].reg_data |= (0x01<<led_idx);
		lm3642_low_array[FLASH_LOW_LED1_INDEX+led_idx].reg_data = torch_level;
	} else {
		lm3642_low_array[FLASH_LOW_CTRL_INDEX].reg_data &= ~(0x01<<led_idx);
	}
	LM3642_DBG("%s: prepare torch_level: %d success\n", __func__, torch_level);
	//LM3642_DBG("%s:lm3642_low_array[0~3]: %x, %x, %x\n", __func__, lm3642_low_array[0].reg_data, lm3642_low_array[1].reg_data, lm3642_low_array[2].reg_data);
	
	return 0;
}

/*
** 
** Flash current level from 0~127
**
*/
int lm3642_flash_level_to_reg(uint32_t level, uint32_t led_idx)
{
	uint8_t flash_level = level;

	if (led_idx > 1) {
		LM3642_DBG("%s: led index: %d exceed limitaion!!!\n", __func__, led_idx);
		return -1;
	}
	
	if (flash_level >127) {
		LM3642_DBG("%s: flash level: %d exceed current limitaion!!!\n", __func__, flash_level);
		//return -1;
		flash_level = 127;
	}

	if (flash_level) {
		lm3642_high_array[FLASH_HIGH_CTRL_INDEX].reg_data |= (0x01<<led_idx);
		lm3642_high_array[FLASH_HIGH_LED1_INDEX+led_idx].reg_data = flash_level;
	} else {
		lm3642_high_array[FLASH_HIGH_CTRL_INDEX].reg_data &= ~(0x01<<led_idx);
	}
	LM3642_DBG("%s: prepare flash%d level: %d success\n", __func__, led_idx, flash_level);
	//LM3642_DBG("%s:lm3642_low_array[0~3]: %x, %x, %x, %x\n", __func__, lm3642_high_array[0].reg_data, lm3642_high_array[1].reg_data, lm3642_high_array[2].reg_data, lm3642_high_array[3].reg_data);
	
	return 0;
}

/*
** 
** Torch current level from 0~127
**
*/
int lm3642_torch_level_to_reg(uint32_t level, uint32_t led_idx)
{
	uint8_t torch_level = level;

	if (led_idx > 1) {
		LM3642_DBG("%s: led index: %d exceed limitaion!!!\n", __func__, led_idx);
		return -1;
	}
	
	if (torch_level >127) {
		LM3642_DBG("%s: torch_level: %d exceed current limitaion!!!\n", __func__, torch_level);
		//return -1;
		torch_level = 100;
	}
	if (torch_level) {
		lm3642_low_array[FLASH_LOW_CTRL_INDEX].reg_data |= (0x01<<led_idx);
		lm3642_low_array[FLASH_LOW_LED1_INDEX+led_idx].reg_data = torch_level;
	} else {
		lm3642_low_array[FLASH_LOW_CTRL_INDEX].reg_data &= ~(0x01<<led_idx);
	}
	LM3642_DBG("%s: prepare torch%d level: %d success\n", __func__, led_idx, torch_level);
	//LM3642_DBG("%s:lm3642_low_array[0~3]: %x, %x, %x\n", __func__, lm3642_low_array[0].reg_data, lm3642_low_array[1].reg_data, lm3642_low_array[2].reg_data);
	
	return 0;
}

int lm3642_flash_duration_set(uint32_t ms, uint32_t led_idx)
{
	if (led_idx > 1) {
		LM3642_DBG("%s: led index: %d exceed limitaion!!!\n", __func__, led_idx);
		return -1;
	}
	
	if (ms > 400) {
		LM3642_DBG("%s: duration: %d exceed limitaion!!!\n", __func__, ms);
		ms = 400;
	}
	
	if (ms <= 100) {
		lm3642_high_array[FLASH_HIGH_DURATION_INDEX].reg_data = (ms/10 + (ms%10? 1 : 0))-1;
	} else {
		lm3642_high_array[FLASH_HIGH_DURATION_INDEX].reg_data = ((ms-100)/50 + ((ms-100)?1:0))+9;
	}

	LM3642_DBG("%s: set led%d duration: %d done~\n", __func__, led_idx, ms);

	return 0;
}

/*+End.*/

static struct msm_camera_i2c_client lm3642_i2c_client = {
	.addr_type = MSM_CAMERA_I2C_BYTE_ADDR,
};

static struct msm_camera_i2c_reg_setting lm3642_init_setting = {
	.reg_setting = lm3642_init_array,
	.size = ARRAY_SIZE(lm3642_init_array),
	.addr_type = MSM_CAMERA_I2C_BYTE_ADDR,
	.data_type = MSM_CAMERA_I2C_BYTE_DATA,
	.delay = 0,
};

static struct msm_camera_i2c_reg_setting lm3642_off_setting = {
	.reg_setting = lm3642_off_array,
	.size = ARRAY_SIZE(lm3642_off_array),
	.addr_type = MSM_CAMERA_I2C_BYTE_ADDR,
	.data_type = MSM_CAMERA_I2C_BYTE_DATA,
	.delay = 0,
};

static struct msm_camera_i2c_reg_setting lm3642_release_setting = {
	.reg_setting = lm3642_release_array,
	.size = ARRAY_SIZE(lm3642_release_array),
	.addr_type = MSM_CAMERA_I2C_BYTE_ADDR,
	.data_type = MSM_CAMERA_I2C_BYTE_DATA,
	.delay = 0,
};

static struct msm_camera_i2c_reg_setting lm3642_low_setting = {
	.reg_setting = lm3642_low_array,
	.size = ARRAY_SIZE(lm3642_low_array),
	.addr_type = MSM_CAMERA_I2C_BYTE_ADDR,
	.data_type = MSM_CAMERA_I2C_BYTE_DATA,
	.delay = 0,
};

static struct msm_camera_i2c_reg_setting lm3642_high_setting = {
	.reg_setting = lm3642_high_array,
	.size = ARRAY_SIZE(lm3642_high_array),
	.addr_type = MSM_CAMERA_I2C_BYTE_ADDR,
	.data_type = MSM_CAMERA_I2C_BYTE_DATA,
	.delay = 0,
};

static struct msm_led_flash_reg_t lm3642_regs = {
	.init_setting = &lm3642_init_setting,
	.off_setting = &lm3642_off_setting,
	.low_setting = &lm3642_low_setting,
	.high_setting = &lm3642_high_setting,
	.release_setting = &lm3642_release_setting,
	.p_flash_source_cur_setting_func_tbl = {
		lm3642_flash_current_to_reg,
		lm3642_torch_current_to_reg,
		lm3642_flash_level_to_reg,
		lm3642_torch_level_to_reg,
		lm3642_flash_duration_set,
	},
};

static struct msm_flash_fn_t lm3642_func_tbl = 
/*lenovo-sw chenglong1 add for flash drv*/
#if 0
{
	.flash_get_subdev_id = msm_led_i2c_trigger_get_subdev_id,
	.flash_led_config = msm_led_i2c_trigger_config,
	.flash_led_init = msm_flash_lm3642_led_init,
	.flash_led_release = msm_flash_lm3642_led_release,
	.flash_led_off = msm_flash_lm3642_led_off,
	.flash_led_low = msm_flash_lm3642_led_low,
	.flash_led_high = msm_flash_lm3642_led_high,
};
#else
{
	.flash_get_subdev_id = msm_led_i2c_trigger_get_subdev_id,
	.flash_led_config = msm_led_i2c_trigger_config,
	.flash_led_init = msm_flash_led_init,
	.flash_led_release = msm_flash_led_release,
	.flash_led_off = msm_flash_led_off,
	.flash_led_low = msm_flash_led_low,
	.flash_led_high = msm_flash_led_high,
};
#endif
/*lenovo-sw add end*/


static struct msm_led_flash_ctrl_t fctrl = {
	.flash_i2c_client = &lm3642_i2c_client,
	.reg_setting = &lm3642_regs,
	.func_tbl = &lm3642_func_tbl,
};

module_init(msm_flash_lm3642_init);
module_exit(msm_flash_lm3642_exit);
MODULE_DESCRIPTION("lm3642 FLASH");
MODULE_LICENSE("GPL v2");
