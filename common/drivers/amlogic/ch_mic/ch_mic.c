#include <linux/types.h>
#include <linux/pm.h>
#include <linux/gpio.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/fsl_devices.h>
#include <asm/setup.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/stat.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/spinlock.h>
#include <linux/err.h>
#include <linux/regulator/consumer.h>

int ch_mic_pin = -1;
int mic_flag = -1;
static struct class *ch_mic_class = NULL;
static struct device *ch_mic_dev = NULL;

#define CTL_POWER_ON "1"
#define CTL_POWER_OFF "0"

static ssize_t gpio_mic_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	pr_info("%s\n", __func__);
	sprintf(buf, "gpio_mic is %d\n", mic_flag);
	return strlen(buf);
}

static ssize_t gpio_mic_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	if(!strncmp(buf, CTL_POWER_ON, strlen(CTL_POWER_ON))) {
		pr_info("%s: to enable gpio_mic\n", __func__);
		gpio_set_value(ch_mic_pin, 1);
		mic_flag = 1;
	} else if(!strncmp(buf, CTL_POWER_OFF, strlen(CTL_POWER_OFF))) {
		pr_info("%s: to disable gpio_mic\n", __func__);
		gpio_set_value(ch_mic_pin, 0);
		mic_flag = 0;
	}
	return count;
}

static struct device_attribute gpio_mic_dev_attr = {
	.attr = {
		.name = "gpio_mic",
		.mode = S_IRWXU|S_IRWXG|S_IRWXO,
	},
	.show = gpio_mic_show,
	.store = gpio_mic_store,
};

static int ch_mic_probe(struct platform_device *pdev)
{
	int ret = 0;

	pr_info("xcz enter ch_mic_probe \n");

	ch_mic_pin = of_get_named_gpio(pdev->dev.of_node, "qcom,ch_mic_pin", 0);
	if (ch_mic_pin < 0)
		pr_info("xcz ch_mic_pin is not available \n");

	pr_info("[ch_mic]The GPIO NUM IS %d\n", ch_mic_pin);

	ret = gpio_request(ch_mic_pin, "ch_mic_pin");
	if(0 != ret) {
		pr_info("xcz gpio request %d failed.", ch_mic_pin);
		goto fail1;
	}

	gpio_direction_output(ch_mic_pin, 0);

	gpio_set_value(ch_mic_pin, 0);
	mic_flag = 0;

	ch_mic_class = class_create(THIS_MODULE, "ch_mic");
	if(IS_ERR(ch_mic_class))
	{
		ret = PTR_ERR(ch_mic_class);
		pr_info("Failed to create class.\n");
		return ret;
	}

	ch_mic_dev = device_create(ch_mic_class, NULL, 0, NULL, "gpio_mic");
	if (IS_ERR(ch_mic_dev))
	{
		ret = PTR_ERR(ch_mic_class);
		pr_info("Failed to create device(ch_mic_dev)!\n");
		return ret;
	}

	ret = device_create_file(ch_mic_dev, &gpio_mic_dev_attr);
	if(ret)
	{
		pr_err("%s: gpio_mic creat sysfs failed\n",__func__);
		return ret;
	}

	pr_info("xcz enter ch_mic_probe, ok \n");

	fail1:
	return ret;
}

static int ch_mic_remove(struct platform_device *pdev)
{
	device_destroy(ch_mic_class, 0);
	class_destroy(ch_mic_class);
	device_remove_file(ch_mic_dev, &gpio_mic_dev_attr);

	return 0;
}

static int ch_mic_suspend(struct platform_device *pdev,pm_message_t state)
{
	return 0;
}

static int ch_mic_resume(struct platform_device *pdev)
{
	return 0;
}

static struct of_device_id ch_mic_power_dt_match[] = {
	{ .compatible = "amlogic, chMic",},
	{ },
};
MODULE_DEVICE_TABLE(of, ch_mic_power_dt_match);

static struct platform_driver gpio_mic_driver = {
	.driver = {
		.name = "ch_mic_power",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(ch_mic_power_dt_match),
	},
	.probe = ch_mic_probe,
	.remove = ch_mic_remove,
	.suspend = ch_mic_suspend,
	.resume = ch_mic_resume,
};

static __init int gpio_mic_init(void)
{
	return platform_driver_register(&gpio_mic_driver);
}

static void __exit gpio_mic_exit(void)
{
	platform_driver_unregister(&gpio_mic_driver);
}

module_init(gpio_mic_init);
module_exit(gpio_mic_exit);
MODULE_AUTHOR("ch_mic_POWER, Inc.");
MODULE_DESCRIPTION("Victor ch_mic_POWER");
MODULE_LICENSE("GPL");
