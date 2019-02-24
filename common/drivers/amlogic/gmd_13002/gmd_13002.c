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
#include <Fonts.h>
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

#define AML_I2C_BUS_AO		0
#define AML_I2C_BUS_A		1
#define AML_I2C_BUS_B		2
#define AML_I2C_BUS_C		3

#define GMD13002_CMD    0
#define GMD13002_DAT    1

#define GMD13002_WIDTH    128
#define GMD13002_HEIGHT   64

#define __SET_COL_START_ADDR() 	do { \
									gmd13002_write(0x00, GMD13002_CMD); \
									gmd13002_write(0x10, GMD13002_CMD); \
								} while(false)

static uint8_t s_chDispalyBuffer[128][8];

struct i2c_client *g_aml_gmd13002_client = NULL;
struct aml_gmd13002_data *gmd = NULL;

void gmd13002_clear_screen(uint8_t chFill);
#define GMD_IDEVICE_NAME "gmd_13002"
struct aml_gmd13002_data 
{
	struct i2c_client *client;	
	struct input_dev *gmd_dev;
	struct workqueue_struct *gmd_mems_work_queue;
	unsigned int key_flag;
	unsigned int cnt;

};
/*
static s32 atoi(char *psz_buf)
{
	char *pch = psz_buf;
	s32 base = 0;

	while (isspace(*pch))
		pch++;

	if (*pch == '-' || *pch == '+') {
		base = 10;
		pch++;
	} else if (*pch && tolower(pch[strlen(pch) - 1]) == 'h') {
		base = 16;
	}

	return simple_strtoul(pch, NULL, base);
}
*/
int gmd_i2c_write(uint8_t *writebuf, uint8_t writelen)
{
    int ret = 0;

    //mutex_lock(&i2c_rw_access);
    if (writelen > 0)
    {
        struct i2c_msg msgs[] =
        {
            {
                .addr = g_aml_gmd13002_client->addr,
                .flags = 0,
                .len = writelen,
                .buf = writebuf,
            },
        };
        ret = i2c_transfer(g_aml_gmd13002_client->adapter, msgs, 1);
        if (ret < 0)
        {
            pr_err("%s: i2c_transfer(write) err, ret=%d", __func__, ret);
        }
    }
   // mutex_unlock(&i2c_rw_access);

    return ret;
}

static void gmd13002_write(uint8_t chData, uint8_t chCmd)
{
	uint8_t cmd_buf[2] = {0};

	if (chCmd) 
	{	
		cmd_buf[0] = 0x40;
		cmd_buf[1] = chData;
		gmd_i2c_write(cmd_buf,sizeof(cmd_buf));
	} 
	else
	{
		cmd_buf[0] = 0x00;
		cmd_buf[1] = chData;
		gmd_i2c_write(cmd_buf,sizeof(cmd_buf));
	}
}

static void gmd13002_conifg(void)
{
	gmd13002_write(0xAE, GMD13002_CMD);//--turn off oled panel
	gmd13002_write(0xd5, GMD13002_CMD);//--set display clock divide ratio/oscillator frequency
	gmd13002_write(0x80, GMD13002_CMD);//--set divide ratio, Set Clock as 100 Frames/Sec
	gmd13002_write(0xA8, GMD13002_CMD);//--set multiplex ratio(1 to 64)
	gmd13002_write(0x3f, GMD13002_CMD);//--1/64 duty
	gmd13002_write(0xD3, GMD13002_CMD);//-set display offset	Shift Mapping RAM Counter (0x00~0x3F)
	gmd13002_write(0x00, GMD13002_CMD);//-not offset
	gmd13002_write(0x40, GMD13002_CMD);//--set start line address  Set Mapping RAM Display Start Line (0x00~0x3F)
	gmd13002_write(0xA1, GMD13002_CMD);//--Set SEG/Column Mapping  
	gmd13002_write(0xC0, GMD13002_CMD);//--Set SEG/Column Mapping  
	gmd13002_write(0xDA, GMD13002_CMD);//--set com pins hardware configuration
	gmd13002_write(0x12, GMD13002_CMD);
	gmd13002_write(0x81, GMD13002_CMD);//--set contrast control register
	gmd13002_write(0x66, GMD13002_CMD);
	gmd13002_write(0xD9, GMD13002_CMD);//--set pre-charge period
	gmd13002_write(0xF1, GMD13002_CMD);//Set Pre-Charge as 15 Clocks & Discharge as 1 Clock
	gmd13002_write(0xDB, GMD13002_CMD);//--set vcomh
	gmd13002_write(0x30, GMD13002_CMD);
	gmd13002_write(0xA4, GMD13002_CMD);// Disable Entire Display On (0xa4/0xa5)
	gmd13002_write(0xA6, GMD13002_CMD);// Disable Inverse Display On (0xa6/a7) 
	gmd13002_clear_screen(0x00);
	gmd13002_write(0x8D, GMD13002_CMD);//--set Charge Pump enable/disable
	gmd13002_write(0x14, GMD13002_CMD);//--set(0x10) disable
	gmd13002_write(0xAF, GMD13002_CMD);//--turn on oled panel
	mdelay(100);
}


void gmd13002_refresh_gram(void)
{
	uint8_t i, j;
	for (i = 0; i < 8; i ++) {  
		gmd13002_write(0xB0 + i, GMD13002_CMD);    
		__SET_COL_START_ADDR();      
		for (j = 0; j < 128; j ++) {
			gmd13002_write(s_chDispalyBuffer[j][i], GMD13002_DAT); 
		}
	}   
}


void gmd13002_clear_screen(uint8_t chFill)  
{ 
	uint8_t i, j;
	
	for (i = 0; i < 8; i ++) {
		gmd13002_write(0xB0 + i, GMD13002_CMD);
		__SET_COL_START_ADDR();
		for (j = 0; j < 128; j ++) {
			s_chDispalyBuffer[j][i] = chFill;
		}
	}
	
	gmd13002_refresh_gram();
}

void gmd13002_draw_point(uint8_t chXpos, uint8_t chYpos, uint8_t chPoint)
{
	uint8_t chPos, chBx, chTemp = 0;
	
	if (chXpos > 127 || chYpos > 63) {
		return;
	}
	chPos = 7 - chYpos / 8; // 
	chBx = chYpos % 8;
	chTemp = 1 << (7 - chBx);
	
	if (chPoint) {
		s_chDispalyBuffer[chXpos][chPos] |= chTemp;
		
	} else {
		s_chDispalyBuffer[chXpos][chPos] &= ~chTemp;
	}
}

void gmd13002_display_char(uint8_t chXpos, uint8_t chYpos, uint8_t chChr, uint8_t chSize, uint8_t chMode)
{      	
	uint8_t i, j;
	uint8_t chTemp, chYpos0 = chYpos;
	
	chChr = chChr - ' ';				   
    for (i = 0; i < chSize; i ++) {   
		if (chSize == 12) {
			if (chMode) {
				chTemp = c_chFont1206[chChr][i];
			} else {
				chTemp = ~c_chFont1206[chChr][i];
			}
		} else {
			if (chMode) {
				chTemp = c_chFont1608[chChr][i];
			} else {
				chTemp = ~c_chFont1608[chChr][i];
			}
		}
		
        for (j = 0; j < 8; j ++) {
			if (chTemp & 0x80) {
				gmd13002_draw_point(chXpos, chYpos, 1);
			} else {
				gmd13002_draw_point(chXpos, chYpos, 0);
			}
			chTemp <<= 1;
			chYpos ++;
			
			if ((chYpos - chYpos0) == chSize) {
				chYpos = chYpos0;
				chXpos ++;
				break;
			}
		}  	 
    } 
}


void gmd13002_display_string(uint8_t chXpos, uint8_t chYpos, const uint8_t *pchString, uint8_t chSize, uint8_t chMode)
{
    while (*pchString != '\0') {       
        if (chXpos > (GMD13002_WIDTH - chSize / 2)) {
			chXpos = 0;
			//chYpos += chSize;
			//if (chYpos > (GMD13002_HEIGHT - chSize)) {
				//chYpos = chXpos = 0;
				//gmd13002_clear_screen(0x00);
			//}
			break;
		}
		
        gmd13002_display_char(chXpos, chYpos, *pchString, chSize, chMode);
        chXpos += chSize / 2;
        pchString ++;
    }
}

static int aml_gmd13002_i2c_init(void)
{
	gmd13002_conifg();
	gmd13002_clear_screen(0xFF);
	mdelay(1000);
	gmd13002_clear_screen(0x00);
	return 0;
}

//*****************************事件接收与发送******************************//
static ssize_t gmd13002_send_store(struct device *dev,struct device_attribute *attr, const char *buf, size_t count)
{
	uint8_t i = 0,t = 0;
	uint8_t x1_addr,y1_addr,mid_addr;
	char 		send_buf1[32]={0};
	char mid_buf[32]={0};
	uint8_t X1_buf[2]={0};
	uint8_t Y1_buf[2]={0};
	gmd13002_clear_screen(0x00);
	while( *(buf+2) != '\0' )
	{
		while((*(buf+t)!= '\r')||(*(buf+t+1) != '\n'))
		{
			t++;              
			if( (t > 64) ||	(*(buf+t) == '\r'&&*(buf+t+1) != '\n') )
			goto exit_send_store_err;
		}
		for(i = 0;i < t; i++)
		{
		if( i < 2 ){
			X1_buf[i] = *(buf+i);
			pr_info("X1_buf[i] = %d ,i = %d \n", X1_buf[i],i);
		}
		else if( (2 <= i)&&(i < 4 )){
			Y1_buf[i - 2] = *(buf+i);
			pr_info("Y1_buf[i] = %d ,i = %d \n", Y1_buf[i - 2],i);
		}
		else{
			mid_buf[i - 4] = *(buf+i);
			pr_info("mid_buf[i] = %d ,i = %d \n", mid_buf[i - 4],i);
		}
		}
		strncpy(send_buf1,mid_buf,i-4);
		mid_addr = X1_buf[1]-48;
		x1_addr = X1_buf[0]-48;
		if( x1_addr == 0 )
			x1_addr = mid_addr;
		else
			x1_addr = (x1_addr * 10 + mid_addr);
		mid_addr = Y1_buf[1]-48;
		y1_addr = Y1_buf[0]-48;
		if( y1_addr == 0 )
			y1_addr = mid_addr;
		else
			y1_addr = (y1_addr * 10 + mid_addr);

		pr_info("x_addr1 = %d ,y_addr1 = %d \n", x1_addr, y1_addr);
		gmd13002_display_string(x1_addr, y1_addr, send_buf1, 16, 1);
		gmd13002_refresh_gram();	
		buf = buf+t+2;
		t = 0;	
	}
	return count;
	exit_send_store_err:
	pr_info("SEND ERRO: buf exceed 64 or End not write \\r\\n \n");
	return count;	
}

static ssize_t gmd13002_send_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return 0; 	
}


static DEVICE_ATTR(gmd_13002, S_IRUGO|S_IWUSR|S_IWGRP|S_IWOTH|S_IROTH,gmd13002_send_show, gmd13002_send_store);

static struct attribute *gmd_attributes[] = {
	&dev_attr_gmd_13002.attr,
	NULL
};

static struct attribute_group gmd_attribute_group = {
	.attrs = gmd_attributes
};

static int aml_gmd13002_i2c_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	int err;
	gmd = kzalloc(sizeof(struct aml_gmd13002_data),GFP_KERNEL);
	if (!gmd) 
	{	
		printk(KERN_ERR "%s:memory allocation err\n", __func__);
		err = -ENOMEM;
		goto exit_kzalloc_err;
	}

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		pr_err("i2c check err, dev_id=%s--\n", id->name);
		return -ENODEV;
	}
	i2c_set_clientdata(client, NULL);
	g_aml_gmd13002_client = client;
	aml_gmd13002_i2c_init();

	err = sysfs_create_group(&client->dev.kobj, &gmd_attribute_group);	//sysfs接口创建驱动对应的属性
	if (err) 
	{
		printk(KERN_ERR "%s: sysfs_create_group failed\n", __func__);
		goto exit_sysfs_create_group_err;
	}

	printk(KERN_ERR "%s successfully  probe >>>>>>>>>\n", __func__);	
	return 0;

	exit_sysfs_create_group_err:
	kfree(gmd);	
	gmd = NULL;	
	exit_kzalloc_err:
	return err;
}

static int aml_gmd13002_i2c_remove(struct i2c_client *client)
{
	pr_info("enter %s\n", __func__);
	gmd13002_write(0xAE, GMD13002_CMD);//--turn off oled panel
	gmd13002_write(0x8D, GMD13002_CMD);
	gmd13002_write(0x10, GMD13002_CMD);
	mdelay(100);
	sysfs_remove_group(&client->dev.kobj, &gmd_attribute_group);
	kfree(i2c_get_clientdata(client));
	return 0;
}

static int aml_gmd13002_probe(struct platform_device *pdev)
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
		board_info.of_node = child;	/* for device driver */
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

static int aml_gmd13002_remove(struct platform_device *pdev)
{
	return 0;
}

static const struct of_device_id aml_gmd13002_dt_match[] = {
	{
	 .compatible = "amlogic, gmd13002_prober",
	 },
	{}
};
static struct platform_driver aml_gmd13002_prober = {
	.probe = aml_gmd13002_probe,
	.remove = aml_gmd13002_remove,
	.driver = {
		   .name = "gmd13002_prober",
		   .owner = THIS_MODULE,
		   .of_match_table = aml_gmd13002_dt_match,
		   },
};

static const struct i2c_device_id aml_gmd13002_id_table[] = {
	{"aml_gmd13002", 0},
	{}
};

static const struct of_device_id aml_gmd13002_match_id = {
	.compatible = "amlogic, gmd13002_prober_i2c",
};

static struct i2c_driver aml_gmd13002_i2c_driver = {
	.driver = {
		   .name = "gmd13002",
		   .owner = THIS_MODULE,
		   .of_match_table = &aml_gmd13002_match_id,
		   },
	.probe = aml_gmd13002_i2c_probe,
	.remove = aml_gmd13002_i2c_remove,
	//.resume = aml_pmu4_i2c_resume,
	.id_table = aml_gmd13002_id_table,
};


static int __init aml_gmd13002_modinit(void)
{
	int ret;
	ret = platform_driver_register(&aml_gmd13002_prober);
	return i2c_add_driver(&aml_gmd13002_i2c_driver);
}
static void __exit aml_gmd13002_modexit(void)
{
	i2c_del_driver(&aml_gmd13002_i2c_driver);
	platform_driver_unregister(&aml_gmd13002_prober);
}

arch_initcall(aml_gmd13002_modinit);
module_exit(aml_gmd13002_modexit);

MODULE_DESCRIPTION("Amlogic gmd_13002 device driver");
MODULE_AUTHOR("YEKER");
MODULE_LICENSE("GPL");
