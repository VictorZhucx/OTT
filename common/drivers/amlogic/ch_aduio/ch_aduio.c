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

int ch_audio_pin = -1;
int gpio_flag = -1;
static struct class *ch_audio_class = NULL;
static struct device *ch_audio_dev = NULL;

#define CTL_POWER_ON "1"
#define CTL_POWER_OFF "0"

static ssize_t gpio_audio_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	pr_info("%s\n", __func__);
	sprintf(buf, "gpio_audio is %d\n", gpio_flag);
	return strlen(buf);
}

static ssize_t gpio_audio_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	if(!strncmp(buf, CTL_POWER_ON, strlen(CTL_POWER_ON))) {
		pr_info("%s: to enable gpio_audio\n", __func__);
		gpio_set_value(ch_audio_pin, 1);
		gpio_flag = 1;
	} else if(!strncmp(buf, CTL_POWER_OFF, strlen(CTL_POWER_OFF))) {
		pr_info("%s: to disable gpio_audio\n", __func__);
		gpio_set_value(ch_audio_pin, 0);
		gpio_flag = 0;
	}
	return count;
}

static struct device_attribute gpio_audio_dev_attr = {
	.attr = {
		.name = "gpio_audio",
		.mode = S_IRWXU|S_IRWXG|S_IRWXO,
	},
	.show = gpio_audio_show,
	.store = gpio_audio_store,
};

static int ch_audio_probe(struct platform_device *pdev)
{
	int ret = 0;

	pr_info("xcz enter ch_audio_probe \n");

	ch_audio_pin = of_get_named_gpio(pdev->dev.of_node, "qcom,ch_audio_pin", 0);
	if (ch_audio_pin < 0)
		pr_info("xcz ch_audio_pin is not available \n");

	pr_info("[ch_audio]The GPIO NUM IS %d\n", ch_audio_pin);

	ret = gpio_request(ch_audio_pin, "ch_audio_pin");
	if(0 != ret) {
		pr_info("xcz gpio request %d failed.", ch_audio_pin);
		goto fail1;
	}

	gpio_direction_output(ch_audio_pin, 0);

	gpio_set_value(ch_audio_pin, 0);
	gpio_flag = 0;

	ch_audio_class = class_create(THIS_MODULE, "ch_audio");
	if(IS_ERR(ch_audio_class))
	{
		ret = PTR_ERR(ch_audio_class);
		pr_info("Failed to create class.\n");
		return ret;
	}

	ch_audio_dev = device_create(ch_audio_class, NULL, 0, NULL, "gpio_audio");
	if (IS_ERR(ch_audio_dev))
	{
		ret = PTR_ERR(ch_audio_class);
		pr_info("Failed to create device(ch_audio_dev)!\n");
		return ret;
	}

	ret = device_create_file(ch_audio_dev, &gpio_audio_dev_attr);
	if(ret)
	{
		pr_err("%s: gpio_audio creat sysfs failed\n",__func__);
		return ret;
	}

	pr_info("xcz enter ch_audio_probe, ok \n");

	fail1:
	return ret;
}

static int ch_audio_remove(struct platform_device *pdev)
{
	device_destroy(ch_audio_class, 0);
	class_destroy(ch_audio_class);
	device_remove_file(ch_audio_dev, &gpio_audio_dev_attr);

	return 0;
}

static int ch_audio_suspend(struct platform_device *pdev,pm_message_t state)
{
	return 0;
}

static int ch_audio_resume(struct platform_device *pdev)
{
	return 0;
}

static struct of_device_id ch_audio_power_dt_match[] = {
	{ .compatible = "amlogic, chAudio",},
	{ },
};
MODULE_DEVICE_TABLE(of, ch_audio_power_dt_match);

static struct platform_driver gpio_audio_driver = {
	.driver = {
		.name = "ch_audio_power",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(ch_audio_power_dt_match),
	},
	.probe = ch_audio_probe,
	.remove = ch_audio_remove,
	.suspend = ch_audio_suspend,
	.resume = ch_audio_resume,
};

static __init int gpio_audio_init(void)
{
	return platform_driver_register(&gpio_audio_driver);
}

static void __exit gpio_audio_exit(void)
{
	platform_driver_unregister(&gpio_audio_driver);
}

module_init(gpio_audio_init);
module_exit(gpio_audio_exit);
MODULE_AUTHOR("ch_audio_POWER, Inc.");
MODULE_DESCRIPTION("Victor ch_audio_POWER");
MODULE_LICENSE("GPL");
