#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/pinctrl/consumer.h>
#include <linux/i2c.h>
#include <linux/pm.h>
#include <linux/miscdevice.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/debugfs.h>
#include <linux/uaccess.h>
#include <linux/of_irq.h>
#include <linux/workqueue.h>
#include <linux/sys.h>
#include <linux/input.h>
#include <linux/fs.h>   
#include <linux/syscalls.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/mutex.h>
#include <linux/completion.h>
#include <linux/fcntl.h>
#include <linux/kthread.h>
#include <linux/types.h>
#include <linux/gpio.h>
#include <linux/types.h>              
#include <linux/pm.h>
#include <linux/fsl_devices.h>
#include <asm/setup.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/stat.h>
#include <linux/spinlock.h>
#include <linux/err.h>
#include <linux/regulator/consumer.h>
#include <linux/amlogic/aml_gpio_consumer.h> 

#define AML_I2C_BUS_AO      0
#define AML_I2C_BUS_A       1
#define AML_I2C_BUS_B       2
#define AML_I2C_BUS_C       3
#define INT_GPIO_0  96

int ctrl_mic_pin = 0;
int ctrl_spk_pin = 0;
int i2c_init_flag = 0;
bool earphone_mic = false;
bool earphone_spk = false;
bool user_enable_kala_flag = false;
bool user_play_flag = false;
bool user_record_flag = false;
#define CTRL_IDEVICE_NAME "ctrl_mic"

struct i2c_client *g_aml_ctrl_mic_client = NULL;
struct aml_ctrl_mic_data *cmic = NULL;
struct gpioriq_demo_pdata {
    struct gpio_desc * mic_gpio;
    unsigned int mic_irq_in;
    unsigned int mic_irq_out;
    struct gpio_desc * spk_gpio;
    unsigned int spk_irq_in;
    unsigned int spk_irq_out;
};
struct aml_ctrl_mic_data 
{
    struct i2c_client *client;
};

int ctrl_mic_i2c_read(uint8_t reg)
{
    int ret = 0;
    uint8_t acBuf[1] = {0};
    uint8_t acData[1] = {0};
    struct i2c_msg msgs[] = {
        {
            .addr = 0x10,
            .flags = 0,
            .len = 1,
            .buf = acBuf,
        },
        {
            .addr = 0x10,
            .flags = I2C_M_RD,
            .len = 1,
            .buf = acData,
        },
    };

    acBuf[0] = reg;
    ret = i2c_transfer(g_aml_ctrl_mic_client->adapter, msgs, 2);
    if (ret < 0)
    {
        pr_err("%s: i2c_transfer(read) err, reg is 0x%x, val is 0x%x, ret=%d\n", __func__, reg, acData[0], ret);
        return ret;
    }
    pr_info("%s: i2c_transfer(read), reg is 0x%x, val is 0x%x\n", __func__, reg, acData[0]);
    return 0;
}

int ctrl_mic_i2c_write(uint8_t reg, uint8_t val)
{
    int ret = 0;
    
    uint8_t acBuf[2] = {0};

    //mutex_lock(&i2c_rw_access);
    struct i2c_msg msgs[] =
    {
        {
            .addr = 0x10,
            .flags = 0,
            .len = 2,
            .buf = acBuf,
        },
    };

    acBuf[0] = reg;
    acBuf[1] = val;
    if (i2c_init_flag == 0) {
        return 0;
    }
    ret = i2c_transfer(g_aml_ctrl_mic_client->adapter, msgs, 1);
    if (ret < 0)
    {
        pr_err("%s: i2c_transfer(write) err, reg is 0x%x, val is 0x%x, ret=%d", __func__, reg, val, ret);
    }
   // mutex_unlock(&i2c_rw_access);
   

    return ret;
}

static void process_vol(void) 
{
    // kala ok mode
    if ( (user_record_flag == false) && (user_play_flag == false) && (user_enable_kala_flag == true))
    {
        if (earphone_mic == false)
        {
            ctrl_mic_i2c_write(0x21, 0x24);
        }
        else
        {
            ctrl_mic_i2c_write(0x21, 0x14);
        }

        if (earphone_spk == false)
        {
            ctrl_mic_i2c_write(0x1A, 0xC4);
            ctrl_mic_i2c_write(0x1C, 0xD0);
            ctrl_mic_i2c_write(0x1E, 0xA0);
        }
        else
        {
            ctrl_mic_i2c_write(0x1A, 0xE4);
            ctrl_mic_i2c_write(0x1C, 0xD0);
            ctrl_mic_i2c_write(0x1E, 0x40);
        }
    }
    // ip phone mode
    else
    {
        if (earphone_mic == false)
        {
            ctrl_mic_i2c_write(0x21, 0x24);
        }
        else
        {
            ctrl_mic_i2c_write(0x21, 0x14);
        }

        if (earphone_spk == false)
        {
            ctrl_mic_i2c_write(0x1A, 0xA0);
            ctrl_mic_i2c_write(0x1C, 0x90);
            ctrl_mic_i2c_write(0x1E, 0xA0);
        }
        else
        {
            ctrl_mic_i2c_write(0x1A, 0xA0);
            ctrl_mic_i2c_write(0x1C, 0x90);
            ctrl_mic_i2c_write(0x1E, 0x40);
        }
    }
}

static irqreturn_t change_mic_board(int irq, void *dev_id)
{
    pr_info("change_mic_board\n");
    earphone_mic = false;
    process_vol();
    return IRQ_HANDLED;
}

static irqreturn_t change_mic_headphone(int irq, void *dev_id)
{
    pr_info("change_mic_headphone\n");
    earphone_mic = true;
    process_vol();
    return IRQ_HANDLED;
}

static irqreturn_t change_spk_board(int irq, void *dev_id)
{
    pr_info("change_spk_board\n");
    earphone_spk = false;
    process_vol();
    return IRQ_HANDLED;
}

static irqreturn_t change_spk_headphone(int irq, void *dev_id)
{
    pr_info("change_spk_headphone\n");
    earphone_spk = true;
    process_vol();
    return IRQ_HANDLED;
}

static int aml_ctrl_mic_i2c_init(struct platform_device *pdev)
{
    int ret;
    int val;
    struct gpioriq_demo_pdata *pdata = platform_get_drvdata(pdev);

    pdata->mic_gpio = devm_gpiod_get(&pdev->dev, "ctrl_mic");
    if (IS_ERR(pdata->mic_gpio)) {
        pr_err("Failed to get ctrl_mic-gpios\n");
        return -EINVAL;
    }
    pdata->mic_irq_in = irq_of_parse_and_map(pdev->dev.of_node, 0);
    pdata->mic_irq_out = irq_of_parse_and_map(pdev->dev.of_node, 1);
    if (!pdata->mic_irq_out) {
        pr_err("failed to get irq\n");
        return -EINVAL;
    }
    pr_info("mic_irq_in = %d, mic_irq_out = %d\n", pdata->mic_irq_in, pdata->mic_irq_out);
    ctrl_mic_pin = desc_to_gpio(pdata->mic_gpio);
    pr_info("[ctrl_mic]The GPIO NUM IS %d\n", ctrl_mic_pin);

    ret = gpiod_direction_input(pdata->mic_gpio);        
    if(0 != ret) {
       pr_info("xcz gpio direction input %d failed.", ctrl_mic_pin);
       return ret;
    }
    
    ret = gpiod_for_irq(pdata->mic_gpio, AML_GPIO_IRQ((pdata->mic_irq_in - INT_GPIO_0), FILTER_NUM7, GPIO_IRQ_FALLING));
    ret = gpiod_for_irq(pdata->mic_gpio, AML_GPIO_IRQ((pdata->mic_irq_out - INT_GPIO_0), FILTER_NUM7, GPIO_IRQ_RISING));
    if(ret) {
        printk(KERN_ERR "fail to request test errno:%d\n", ret);
        return ret;
    }

    ret = devm_request_irq(&pdev->dev, pdata->mic_irq_in, change_mic_headphone, IRQF_DISABLED, "ctrl_mic_headphone", pdata);
    if(ret) {
        printk(KERN_ERR "fail to register test errno:%d\n", ret);
        return ret;
    }

    ret = devm_request_irq(&pdev->dev, pdata->mic_irq_out, change_mic_board, IRQF_DISABLED, "ctrl_mic_board", pdata);
    if(ret) {
        printk(KERN_ERR "fail to register test errno:%d\n", ret);
        return ret;
    }
    /* get gpio value */
    val = gpio_get_value(ctrl_mic_pin);
    if (val == 1) {
        earphone_mic = false;
        pr_info("ctrl mic is high");
    } else {
        earphone_mic = true;
        pr_info("ctrl mic is low");
    }
    return 0;
}

static int aml_ctrl_spk_i2c_init(struct platform_device *pdev)
{
    int ret;
    int val;
    struct gpioriq_demo_pdata *pdata = platform_get_drvdata(pdev);

    pdata->spk_gpio = devm_gpiod_get(&pdev->dev, "ctrl_spk");
    if (IS_ERR(pdata->spk_gpio)) {
      pr_err("Failed to get ctrl_spk-gpios\n");
      return -EINVAL;
    }
    pdata->spk_irq_in = irq_of_parse_and_map(pdev->dev.of_node, 2);
    pdata->spk_irq_out = irq_of_parse_and_map(pdev->dev.of_node, 3);
    if (!pdata->spk_irq_out) {
      pr_err("failed to get irq\n");
      return -EINVAL;
    }
    pr_info("spk_irq_in = %d, spk_irq_out = %d\n", pdata->spk_irq_in, pdata->spk_irq_out);
    ctrl_spk_pin = desc_to_gpio(pdata->spk_gpio);
    pr_info("[ctrl_spk]The GPIO NUM IS %d\n", ctrl_spk_pin);

      ret = gpiod_direction_input(pdata->spk_gpio);        
    if(0 != ret) {
       pr_info("xcz gpio direction input %d failed.", ctrl_spk_pin);
       return ret;
    }
    
    ret = gpiod_for_irq(pdata->spk_gpio, AML_GPIO_IRQ((pdata->spk_irq_in - INT_GPIO_0), FILTER_NUM7, GPIO_IRQ_FALLING));
    ret = gpiod_for_irq(pdata->spk_gpio, AML_GPIO_IRQ((pdata->spk_irq_out - INT_GPIO_0), FILTER_NUM7, GPIO_IRQ_RISING));
    if(ret) {
      printk(KERN_ERR "fail to request test errno:%d\n", ret);
      return ret;
    }

    ret = devm_request_irq(&pdev->dev, pdata->spk_irq_in, change_spk_headphone, IRQF_DISABLED, "ctrl_spk_headphone", pdata);
    if(ret) {
      printk(KERN_ERR "fail to register test errno:%d\n", ret);
      return ret;
    }

    ret = devm_request_irq(&pdev->dev, pdata->spk_irq_out, change_spk_board, IRQF_DISABLED, "ctrl_spk_board", pdata);
    if(ret) {
      printk(KERN_ERR "fail to register test errno:%d\n", ret);
      return ret;
    }
    /* get gpio value */
    val = gpio_get_value(ctrl_spk_pin);
    if (val == 1) {
      pr_info("ctrl spk is high");
      earphone_spk = false;
    } else {
      earphone_spk = true;
      pr_info("ctrl spk is low");
    }
    return 0;
}

int strToInt(char val) {
    int num = 0;
    if ((val >= '0') && (val <= '9')) {
        num = val - '0';
    } else if ((val >= 'a') && (val <= 'f')) {
        num = val - 'a' + 10;
    } else if ((val >= 'A') && (val <= 'F')) {
        num = val - 'A' + 10;
    }
    return num;
}

static ssize_t ctrl_mic_send_store(struct device *dev,struct device_attribute *attr, const char *buf, size_t count)
{
    int i = 0;
    char acReg[5] = {0};
    char acVal[5] = {0};
    int reg, val = 0;
    //int val = 0;
    if (strncmp(buf, "1", 1) == 0) {
       pr_info("open sing and hear(left up)\n");
       ctrl_mic_i2c_write(0x21, 0x24);
       ctrl_mic_i2c_write(0x1A, 0xC4);
       ctrl_mic_i2c_write(0x1C, 0xD0);
       ctrl_mic_i2c_write(0x1E, 0xA0);
       ctrl_mic_i2c_write(0x26, 0x4D);
       for (i = 0; i <= 0x4f; i++) { 
           ctrl_mic_i2c_read(i);
       }
    } else if (strncmp(buf, "0", 1) == 0) {
       ctrl_mic_i2c_write(0x38, 0xc0);
       ctrl_mic_i2c_write(0x25, 0xc0);
       ctrl_mic_i2c_write(0x36, 0x20);
       ctrl_mic_i2c_write(0x37, 0x21);
       ctrl_mic_i2c_write(0x1a, 0x08);
       ctrl_mic_i2c_write(0x1c, 0x10);
       ctrl_mic_i2c_write(0x1d, 0x10);
       ctrl_mic_i2c_write(0x1e, 0x40);
       pr_info("close\n");
    } else if (strncmp(buf, "3", 1) == 0) {
       for (i = 0; i <= 0x4f; i++) { 
           ctrl_mic_i2c_read(i);
       }
    } else if (strncmp(buf, "4", 1) == 0) {
       pr_info("open sing and hear(left down)\n");
       ctrl_mic_i2c_write(0x21, 0x24);
       ctrl_mic_i2c_write(0x1A, 0xE4);
       ctrl_mic_i2c_write(0x1C, 0xD0);
       ctrl_mic_i2c_write(0x1E, 0x40);
       ctrl_mic_i2c_write(0x26, 0x4D);
       for (i = 0; i <= 0x4f; i++) { 
           ctrl_mic_i2c_read(i);
       }
    } else if (strncmp(buf, "c", 1) == 0) {
        if (strlen(buf) >= 9) {
            strncpy(acReg, &buf[1], 4);
            strncpy(acVal, &buf[5], 4);
        }
        reg = strToInt(acReg[2]) * 16 + strToInt(acReg[3]);
        val = strToInt(acVal[2]) * 16 + strToInt(acVal[3]);
        pr_info("reg is 0x%x, val is 0x%x\n", reg, val);
       ctrl_mic_i2c_write(reg, val);
       ctrl_mic_i2c_read(reg);
    } else if (strncmp(buf, "5", 1) == 0) {
       pr_info("open sing and hear(right up)\n");
       ctrl_mic_i2c_write(0x21, 0x14);
       ctrl_mic_i2c_write(0x1A, 0xC4);
       ctrl_mic_i2c_write(0x1C, 0xD0);
       ctrl_mic_i2c_write(0x1E, 0xA0);
       ctrl_mic_i2c_write(0x26, 0x4D);
       for (i = 0; i <= 0x4f; i++) { 
           ctrl_mic_i2c_read(i);
       }
    } else if (strncmp(buf, "6", 1) == 0) {
       pr_info("open sing and hear(right down)\n");
       ctrl_mic_i2c_write(0x21, 0x14);
       ctrl_mic_i2c_write(0x1A, 0xE4);
       ctrl_mic_i2c_write(0x1C, 0xD0);
       ctrl_mic_i2c_write(0x1E, 0x40);
       ctrl_mic_i2c_write(0x26, 0x4D);
       for (i = 0; i <= 0x4f; i++) { 
           ctrl_mic_i2c_read(i);
       }
    } else if (strncmp(buf, "operate_kala", 12) == 0) {
        pr_info("operate kala ok\n");
        user_enable_kala_flag = !user_enable_kala_flag;
        process_vol();
    } else if (strncmp(buf, "open_play", 9) == 0) {
        pr_info("open play\n");
        user_play_flag = true;
        process_vol();
    } else if (strncmp(buf, "close_play", 10) == 0) {
        pr_info("close play\n");
        user_play_flag = false;
        process_vol();
	} else if (strncmp(buf, "open_record", 11) == 0) {
        pr_info("open record\n");
        user_record_flag = true;
        process_vol();
	} else if (strncmp(buf, "close_record", 12) == 0) {
        pr_info("close record\n");
        user_record_flag = false;
        process_vol();
	}
    return count;
}

static ssize_t ctrl_mic_send_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    return 0;   
}

static DEVICE_ATTR(ctrl_mic, S_IRUGO|S_IWUSR|S_IWGRP|S_IWOTH|S_IROTH,ctrl_mic_send_show, ctrl_mic_send_store);

static struct attribute *ctrl_mic_attributes[] = {
    &dev_attr_ctrl_mic.attr,
    NULL
};

static struct attribute_group ctrl_attribute_group = {
    .attrs = ctrl_mic_attributes
};

static int aml_ctrl_mic_i2c_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
    int err;
    int val;
    cmic = kzalloc(sizeof(struct aml_ctrl_mic_data),GFP_KERNEL);
    if (!cmic) 
    {   
        printk(KERN_ERR "%s:memory allocation err\n", __func__);
        err = -ENOMEM;
        return err;
    }

    if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
        pr_err("i2c check err, dev_id=%s--\n", id->name);
        kfree(cmic);
        return -ENODEV;
    }
    i2c_set_clientdata(client, NULL);
    g_aml_ctrl_mic_client = client;

    err = sysfs_create_group(&client->dev.kobj, &ctrl_attribute_group); //sysfs
    if (err) 
    {
        printk(KERN_ERR "%s: sysfs_create_group failed\n", __func__);
        kfree(cmic);    
        cmic = NULL;
        return err;
    }

    printk(KERN_ERR "%s successfully  probe >>>>>>>>>\n", __func__);
    i2c_init_flag = 1;
    if (ctrl_mic_pin != 0) {
        /* get gpio value */
        val = gpio_get_value(ctrl_mic_pin);
        if (val == 1) {
        	earphone_mic = false;
            pr_info("ctrl mic is high");
        } else {
        	earphone_mic = true;
            pr_info("ctrl mic is low");
        }
    }
    if (ctrl_spk_pin != 0) {
        /* get gpio value */
        val = gpio_get_value(ctrl_spk_pin);
        if (val == 1) {
        	earphone_spk = false;
            pr_info("ctrl spk is high");
        } else {
        	earphone_spk = true;
            pr_info("ctrl spk is low");
        }
    }
    return 0;
}

static int aml_ctrl_mic_i2c_remove(struct i2c_client *client)
{
    pr_info("enter %s\n", __func__);
    kfree(i2c_get_clientdata(client));
    return 0;
}

static int aml_ctrl_mic_probe(struct platform_device *pdev)
{
    struct device_node *pmu_node = pdev->dev.of_node;
    struct device_node *child;
    struct i2c_board_info board_info;
    struct i2c_adapter *adapter;
    struct i2c_client *client;
    int err;
    int addr;
    int bus_type = -1;
    const char *str;
    struct gpioriq_demo_pdata *pdata;

    pdata = kzalloc(sizeof(struct gpioriq_demo_pdata), GFP_KERNEL);
    if (!pdata) {
        pr_err("failed to kzalloc\n");
        return -ENOMEM;
    }
    platform_set_drvdata(pdev, pdata);

    aml_ctrl_mic_i2c_init(pdev);
    aml_ctrl_spk_i2c_init(pdev);

    for_each_child_of_node(pmu_node, child) {
        /* register exist pmu */
        pr_info("%s, child name:%s\n", __func__, child->name);
        err = of_property_read_string(child, "i2c_bus", &str);
        if (err) {
            pr_info("get 'i2c_bus' failed, ret:%d\n", err);
            continue;
        }
        if (!strncmp(str, "i2c_bus_ao", 10))
            bus_type = AML_I2C_BUS_AO;
        else if (!strncmp(str, "i2c_bus_c", 9))
            bus_type = AML_I2C_BUS_C;
        else if (!strncmp(str, "i2c_bus_b", 9))
            bus_type = AML_I2C_BUS_B;
        else if (!strncmp(str, "i2c_bus_a", 9))
            bus_type = AML_I2C_BUS_A;
        else
            bus_type = AML_I2C_BUS_AO;
        err = of_property_read_string(child, "status", &str);
        if (err) {
            pr_info("get 'status' failed, ret:%d\n", err);
            continue;
        }
        if (strcmp(str, "okay") && strcmp(str, "ok")) {
            /* status is not OK, do not probe it */
            pr_info("device %s status is %s, stop probe it\n",
                child->name, str);
            continue;
        }   
        err = of_property_read_u32(child, "reg", &addr);
        if (err) {
            pr_info("get 'reg' failed, ret:%d\n", err);
            continue;
        }
        memset(&board_info, 0, sizeof(board_info));
        adapter = i2c_get_adapter(bus_type);
        if (!adapter)
            pr_info("wrong i2c adapter:%d\n", bus_type);
        err = of_property_read_string(child, "compatible", &str);
        if (err) {
            pr_info("get 'compatible' failed, ret:%d\n", err);
            continue;
        }
        strncpy(board_info.type, str, I2C_NAME_SIZE);
        board_info.addr = addr;
        pr_info("board_info.addr = %02x \n",board_info.addr);
        board_info.of_node = child; /* for device driver */
        board_info.irq = irq_of_parse_and_map(child, 0);
        client = i2c_new_device(adapter, &board_info);
        if (!client) {
            pr_info("%s, allocate i2c_client failed\n", __func__);
            continue;
        }
        pr_info("%s: adapter:%d, node name:%s, type:%s\n","Allocate new i2c device",bus_type, child->name, str);
    }

    return 0;
}

static int aml_ctrl_mic_remove(struct platform_device *pdev)
{
    return 0;
}

static const struct of_device_id aml_ctrl_mic_dt_match[] = {
    {
     .compatible = "amlogic, ctrl_mic_prober",
     },
    {}
};
static struct platform_driver aml_ctrl_mic_prober = {
    .probe = aml_ctrl_mic_probe,
    .remove = aml_ctrl_mic_remove,
    .driver = {
           .name = "ctrl_mic_prober",
           .owner = THIS_MODULE,
           .of_match_table = aml_ctrl_mic_dt_match,
           },
};

static const struct i2c_device_id aml_ctrl_mic_id_table[] = {
    {"aml_ctrl_mic", 0},
    {}
};

static const struct of_device_id aml_ctrl_mic_match_id = {
    .compatible = "amlogic, ctrl_mic_prober_i2c",
};

static struct i2c_driver aml_ctrl_mic_i2c_driver = {
    .driver = {
           .name = "ctrl_mic",
           .owner = THIS_MODULE,
           .of_match_table = &aml_ctrl_mic_match_id,
           },
    .probe = aml_ctrl_mic_i2c_probe,
    .remove = aml_ctrl_mic_i2c_remove,
    //.resume = aml_pmu4_i2c_resume,
    .id_table = aml_ctrl_mic_id_table,
};


static int __init aml_ctrl_mic_modinit(void)
{
    int ret;
    ret = platform_driver_register(&aml_ctrl_mic_prober);
    return i2c_add_driver(&aml_ctrl_mic_i2c_driver);
}
static void __exit aml_ctrl_mic_modexit(void)
{
    i2c_del_driver(&aml_ctrl_mic_i2c_driver);
    platform_driver_unregister(&aml_ctrl_mic_prober);
}

arch_initcall(aml_ctrl_mic_modinit);
module_exit(aml_ctrl_mic_modexit);

MODULE_DESCRIPTION("Amlogic ctrl_mic device driver");
MODULE_AUTHOR("YEKER");
MODULE_LICENSE("GPL");
