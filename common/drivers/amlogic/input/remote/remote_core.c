/*
 * drivers/amlogic/input/remote/remote_core.c
 *
 * Copyright (C) 2015 Amlogic, Inc. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
*/

#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/types.h>
#include <linux/input.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/mutex.h>
#include <linux/errno.h>
#include <asm/irq.h>
#include <linux/io.h>

/*#include <mach/pinmux.h>*/
#include <linux/major.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/pinctrl/consumer.h>
#include <linux/of_platform.h>
#include <linux/amlogic/cpu_version.h>
#include <linux/amlogic/pm.h>
#include <linux/of_address.h>
#include <linux/fs.h>
#include <asm/segment.h>
#include <asm/uaccess.h>
#include <asm/errno.h>
#include <linux/buffer_head.h>
#include "remote_core.h"

#define SPECIAL_KEY 0x71

/**
  *global variable for debug
  *disable: 0
  *enable: 1
  */
static bool remote_debug_enable;

void remote_repeat(struct remote_dev *dev)
{

}

void remote_debug_set_enable(bool enable)
{
	remote_debug_enable = enable;
}

bool remote_debug_get_enable(void)
{
	return remote_debug_enable;
}

struct file* file_open(const char* path, int flags, int rights) {
    struct file* filp = NULL;
    mm_segment_t oldfs;
    int err = 0;

    oldfs = get_fs();
    set_fs(get_ds());
    filp = filp_open(path, flags, rights);
    set_fs(oldfs);
    if(IS_ERR(filp)) {
        err = PTR_ERR(filp);
        return NULL;
    }
    return filp;
}

void file_close(struct file* file) {
    filp_close(file, NULL);
}

int file_write(struct file* file, unsigned char* data, unsigned int size) {
    mm_segment_t oldfs;
    loff_t pos = 0;
    int ret;

    oldfs = get_fs(); 
    set_fs(get_ds());

    ret = vfs_write(file, data, size, &pos);

    set_fs(oldfs);
    return ret;
}

void toggle_board_spk(void) {
	unsigned char command[] = "operate_kala";
	struct file* ctrl_mic_file;
	ctrl_mic_file = file_open("/sys/devices/i2c-2/2-0020/ctrl_mic", O_WRONLY, 0);
	if (ctrl_mic_file == NULL) {
		return;
	}
	(void)file_write(ctrl_mic_file, command, strlen(command));
	(void)file_close(ctrl_mic_file);
}

static void ir_do_keyup(struct remote_dev *dev)
{
	if (dev->last_keycode == SPECIAL_KEY ) {
		if (dev->wait_cnt <= 15) {
		    input_report_key(dev->input_device, dev->last_keycode, 1);
	        input_report_key(dev->input_device, dev->last_keycode, 0); 
		} else {
			toggle_board_spk();
			remote_dbg(dev->dev, "toggle_board_spk!!\n");
		}
	} else {
		input_report_key(dev->input_device, dev->last_keycode, 0);
	}
	input_sync(dev->input_device);
	dev->keypressed = false;
	dev->last_scancode = -1;
	dev->wait_cnt = 0;
	remote_dbg(dev->dev, "keyup!!\n");
}

static void ir_timer_keyup(unsigned long cookie)
{
	struct remote_dev *dev = (struct remote_dev *)cookie;
	unsigned long flags;

	if (!dev->keypressed)
		return;
	spin_lock_irqsave(&dev->keylock, flags);
	if (dev->is_next_repeat(dev)) {
		dev->keyup_jiffies = jiffies +
			msecs_to_jiffies(dev->keyup_delay);
		mod_timer(&dev->timer_keyup, dev->keyup_jiffies);
		if (dev->wait_cnt <= 1000) {
		    dev->wait_cnt = dev->wait_cnt + 1;
		}
		dev->wait_next_repeat = 1;
		remote_dbg(dev->dev, "wait for repeat, the value is %d\n", dev->wait_cnt);
	} else {
		if (time_is_before_eq_jiffies(dev->keyup_jiffies))
			ir_do_keyup(dev);
		remote_dbg(dev->dev, "no repeat, the value is %d\n", dev->wait_cnt);
		dev->wait_next_repeat = 0;
	}
	spin_unlock_irqrestore(&dev->keylock, flags);
}

static void ir_do_keydown(struct remote_dev *dev, int scancode,
			  u32 keycode)
{
	remote_dbg(dev->dev, "keypressed=0x%x, scancode is %x, keycode is %x\n", dev->keypressed, scancode, keycode);

	if (dev->keypressed)
		ir_do_keyup(dev);

	if (KEY_RESERVED != keycode) {
		dev->keypressed = true;
		dev->last_scancode = scancode;
		dev->last_keycode = keycode;
		if (keycode != SPECIAL_KEY) {
		    input_report_key(dev->input_device, keycode, 1);
		}
		input_sync(dev->input_device);
		remote_dbg(dev->dev, "report key!!\n");
	} else {
		dev_err(dev->dev, "no valid key to handle");
	}
}

void remote_keydown(struct remote_dev *dev, int scancode, int status)
{
	unsigned long flags;
	u32 keycode;

	if (REMOTE_REPEAT != status) {
		if (dev->is_valid_custom &&
			(false == dev->is_valid_custom(dev))) {
			dev_err(dev->dev, "invalid custom:0x%x\n",
				dev->cur_hardcode);
			return;
		}
	}

	spin_lock_irqsave(&dev->keylock, flags);
	/**
	 *only a few keys which be set in key map table support
	 *report relative axes event in mouse mode, other keys
	 *will continue to report key event.
	 */
	if (status == REMOTE_NORMAL ||
			status == REMOTE_REPEAT) {
		/*to report relative axes event*/
		if (0 == dev->ir_report_rel(dev, scancode, status)) {
			spin_unlock_irqrestore(&dev->keylock, flags);
			return;
		}
	}

	if (status == REMOTE_NORMAL) {
		keycode = dev->getkeycode(dev, scancode);
		/*to report key event*/
		ir_do_keydown(dev, scancode, keycode);
	}

	if (dev->keypressed) {
		dev->wait_next_repeat = 0;
		dev->keyup_jiffies = jiffies +
			msecs_to_jiffies(dev->keyup_delay);
		mod_timer(&dev->timer_keyup, dev->keyup_jiffies);
	}
	spin_unlock_irqrestore(&dev->keylock, flags);
}

EXPORT_SYMBOL(remote_keydown);

struct remote_dev *remote_allocate_device(void)
{
	struct remote_dev *dev;

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return NULL;

	dev->input_device = input_allocate_device();
	if (!dev->input_device) {
		kfree(dev);
		return NULL;
	}
	input_set_drvdata(dev->input_device, dev);

	setup_timer(&dev->timer_keyup, ir_timer_keyup, (unsigned long)dev);

	spin_lock_init(&dev->keylock);

	dev->wait_next_repeat = 0;
	return dev;
}
EXPORT_SYMBOL(remote_allocate_device);

void remote_free_device(struct remote_dev *dev)
{
	input_free_device(dev->input_device);
	kfree(dev);
}
EXPORT_SYMBOL(remote_free_device);


int remote_register_device(struct remote_dev *dev)
{
	int i;
	int ret;

	if (MULTI_IR_SOFTWARE_DECODE(dev->rc_type)) {
		remote_raw_init();
		remote_raw_event_register(dev);
	}

	__set_bit(EV_KEY, dev->input_device->evbit);

	for (i = 0; i < KEY_MAX; i++)
		__set_bit(i, dev->input_device->keybit);

	__set_bit(BTN_MOUSE, dev->input_device->keybit);
	__set_bit(BTN_LEFT, dev->input_device->keybit);
	__set_bit(BTN_RIGHT, dev->input_device->keybit);
	__set_bit(BTN_MIDDLE, dev->input_device->keybit);

	__set_bit(EV_REL, dev->input_device->evbit);
	__set_bit(REL_X, dev->input_device->relbit);
	__set_bit(REL_Y, dev->input_device->relbit);
	__set_bit(REL_WHEEL, dev->input_device->relbit);

	dev->input_device->keycodesize = sizeof(unsigned short);
	dev->input_device->keycodemax = 0x1ff;

	ret = input_register_device(dev->input_device);

	dev->debug_current     = 0;
	dev->debug_buffer_size = 4096;
	dev->debug_buffer = kzalloc(dev->debug_buffer_size, GFP_KERNEL);
	if (!dev->debug_buffer) {
		dev_err(dev->dev, "kzalloc debug_buffer error!\n");
		ret = -ENOMEM;
	}

	return ret;
}
EXPORT_SYMBOL(remote_register_device);


void remote_unregister_device(struct remote_dev *dev)
{
	if (MULTI_IR_SOFTWARE_DECODE(dev->rc_type))
		remote_raw_event_unregister(dev);

	input_unregister_device(dev->input_device);
	kfree(dev->debug_buffer);
}
EXPORT_SYMBOL(remote_unregister_device);

MODULE_AUTHOR("AMLOGIC");
MODULE_DESCRIPTION("Remote Driver");
MODULE_LICENSE("GPL");
