#include <linux/module.h>
#include <linux/spi/spi.h>
#include <linux/of.h>
#include <linux/interrupt.h>

#define TOUCH_CMD_X 0xD0
#define TOUCH_CMD_Y 0x90

static int touch_read_adc(struct spi_device *spi, u8 cmd)
{
    u8 tx[3] = { cmd, 0x00, 0x00 };
    u8 rx[3] = { 0 };

    struct spi_transfer transfer = {
        .tx_buf = tx,
        .rx_buf = rx,
        .len = sizeof(tx),
        .speed_hz = 500000,
    };

    int ret;

    ret = spi_sync_transfer(spi, &transfer, 1);
    if (ret)
        return ret;

    return ((rx[1] << 8) | rx[2]) >> 3;
}

static irqreturn_t touch_irq_thread(int irq, void *data)
{
    struct spi_device *spi = data;
    int x, y;

    x = touch_read_adc(spi, TOUCH_CMD_X);
    if (x < 0)
        goto error;

    y = touch_read_adc(spi, TOUCH_CMD_Y);
    if (y < 0)
        goto error;

    dev_info(&spi->dev, "Touch: X=%d, Y=%d\n", x, y);

    return IRQ_HANDLED;

error:
    dev_err(&spi->dev, "SPI read failed\n");
    return IRQ_HANDLED;
}

static int touch_probe(struct spi_device *spi)
{
    int ret;

    dev_info(&spi->dev, "Touch probe started, IRQ=%d\n", spi->irq);

    if (spi->irq <= 0)
        return -EINVAL;

    ret = devm_request_threaded_irq(
        &spi->dev,
        spi->irq,
        NULL,
        touch_irq_thread,
        IRQF_ONESHOT,
        "touch_lab",
        spi
    );

    if (ret) {
        dev_err(&spi->dev, "Failed to request IRQ: %d\n", ret);
        return ret;
    }

    return 0;
}

static void touch_remove(struct spi_device *spi)
{
    dev_info(&spi->dev, "Touch driver removed\n");
}

static const struct of_device_id touch_of_match[] = {
    { .compatible = "learning,spi-touch" },
    { }
};

MODULE_DEVICE_TABLE(of, touch_of_match);

static struct spi_driver touch_driver = {
    .driver = {
        .name = "touch_lab",
        .of_match_table = touch_of_match,
    },
    .probe = touch_probe,
    .remove = touch_remove,
};

module_spi_driver(touch_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Learning SPI Touch Driver");
