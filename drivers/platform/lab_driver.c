#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>

static int lab_probe(struct platform_device *pdev)
{
	dev_info(&pdev->dev, "hello from probe!\n");
	return 0;
}

static void lab_remove(struct platform_device *pdev)
{
	dev_info(&pdev->dev, "goodbye from remove!\n");
}

static const struct of_device_id lab_of_match[] =
{
	{ .compatible = "example,rpi5-dt-lab"},
	{ }
};

MODULE_DEVICE_TABLE(of, lab_of_match);

static struct platform_driver lab_driver = {
	.probe = lab_probe,
	.remove = lab_remove,
	.driver = {
		.name = "lab_driver",
		.of_match_table = lab_of_match,
	},
};

module_platform_driver(lab_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("simple device tree platform driver");
MODULE_AUTHOR("mumi");
