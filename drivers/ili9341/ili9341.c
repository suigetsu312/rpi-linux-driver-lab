
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/spi/spi.h>
#include <linux/gpio/consumer.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/of.h>

#define LCD_WIDTH       240
#define LCD_HEIGHT      320
#define LCD_CHUNK_SIZE  4096

#define ILI9341_SWRESET 0x01
#define ILI9341_SLPOUT  0x11
#define ILI9341_DISPON  0x29
#define ILI9341_CASET   0x2A
#define ILI9341_PASET   0x2B
#define ILI9341_RAMWR   0x2C
#define ILI9341_MADCTL  0x36
#define ILI9341_COLMOD  0x3A

struct ili9341 {
    struct spi_device *spi;
    struct gpio_desc *dc;
    struct gpio_desc *reset;
};

/* D/C = 0: command */
static int ili9341_cmd(struct ili9341 *lcd, u8 cmd)
{
    gpiod_set_value_cansleep(lcd->dc, 0);
    return spi_write(lcd->spi, &cmd, 1);
}

/* D/C = 1: data */
static int ili9341_data(struct ili9341 *lcd,
                        const void *data, size_t len)
{
    gpiod_set_value_cansleep(lcd->dc, 1);
    return spi_write(lcd->spi, data, len);
}

static int ili9341_reg(struct ili9341 *lcd,
                       u8 cmd, const u8 *data, size_t len)
{
    int ret;

    ret = ili9341_cmd(lcd, cmd);
    if (ret)
        return ret;

    if (len == 0)
        return 0;

    return ili9341_data(lcd, data, len);
}

static int ili9341_init(struct ili9341 *lcd)
{
    int ret;

    const u8 madctl = 0x48;
    const u8 colmod = 0x55;  /* RGB565 */

    /* Hardware reset, active-low */
    gpiod_set_value_cansleep(lcd->reset, 1);
    msleep(20);

    gpiod_set_value_cansleep(lcd->reset, 0);
    msleep(120);

    ret = ili9341_cmd(lcd, ILI9341_SWRESET);
    if (ret)
        return ret;

    msleep(150);

    ret = ili9341_cmd(lcd, ILI9341_SLPOUT);
    if (ret)
        return ret;

    msleep(150);

    ret = ili9341_reg(lcd, ILI9341_MADCTL,
                      &madctl, 1);
    if (ret)
        return ret;

    ret = ili9341_reg(lcd, ILI9341_COLMOD,
                      &colmod, 1);
    if (ret)
        return ret;

    ret = ili9341_cmd(lcd, ILI9341_DISPON);
    if (ret)
        return ret;

    msleep(20);
    return 0;
}

static int ili9341_fill_color(struct ili9341 *lcd,
                              u16 x,
                              u16 y,
                              u16 w,
                              u16 h,
                              u16 color)
{
    if (x >= LCD_WIDTH || y >= LCD_HEIGHT)
        return -EINVAL;

    if (w == 0 || h == 0)
        return -EINVAL;

    if (w > LCD_WIDTH - x ||
        h > LCD_HEIGHT - y)
        return -EINVAL;

    const u8 col_addr[] = {
        (x)>> 8, (x) & 0xff,
        (x+w)>> 8, (x+w) & 0xff,
    };

    const u8 page_addr[] = {
        (y)>> 8, (y) & 0xff,
        (y+h)>> 8, (y+h) & 0xff,
    };

        u8 *buf;
    size_t remaining;
    size_t i;
    int ret;

    buf = kmalloc(LCD_CHUNK_SIZE, GFP_KERNEL);
    if (!buf)
        return -ENOMEM;

    /* RGB565 red = 0xF800, high byte first */
    for (i = 0; i < LCD_CHUNK_SIZE; i += 2) {
        buf[i] = color>>8;
        buf[i + 1] = color&0xff;
    }

    ret = ili9341_reg(lcd, ILI9341_CASET,
                      col_addr, sizeof(col_addr));
    if (ret)
        goto out;

    ret = ili9341_reg(lcd, ILI9341_PASET,
                      page_addr, sizeof(page_addr));
    if (ret)
        goto out;

    ret = ili9341_cmd(lcd, ILI9341_RAMWR);
    if (ret)
        goto out;

    remaining = (size_t)w * h * 2;

    while (remaining > 0) {
        size_t len = min_t(size_t, remaining,
                           LCD_CHUNK_SIZE);

        ret = ili9341_data(lcd, buf, len);
        if (ret)
            goto out;

        remaining -= len;
    }

out:
    kfree(buf);
    return ret;


}


static int ili9341_fill_red(struct ili9341 *lcd)
{
    const u8 col_addr[] = {
        0x00, 0x00,
        0x00, LCD_WIDTH - 1
    };

    const u8 page_addr[] = {
        0x00, 0x00,
        (LCD_HEIGHT - 1) >> 8,
        (LCD_HEIGHT - 1) & 0xff
    };

    u8 *buf;
    size_t remaining;
    size_t i;
    int ret;

    buf = kmalloc(LCD_CHUNK_SIZE, GFP_KERNEL);
    if (!buf)
        return -ENOMEM;

    /* RGB565 red = 0xF800, high byte first */
    for (i = 0; i < LCD_CHUNK_SIZE; i += 2) {
        buf[i] = 0xF8;
        buf[i + 1] = 0x00;
    }

    ret = ili9341_reg(lcd, ILI9341_CASET,
                      col_addr, sizeof(col_addr));
    if (ret)
        goto out;

    ret = ili9341_reg(lcd, ILI9341_PASET,
                      page_addr, sizeof(page_addr));
    if (ret)
        goto out;

    ret = ili9341_cmd(lcd, ILI9341_RAMWR);
    if (ret)
        goto out;

    remaining = LCD_WIDTH * LCD_HEIGHT * 2;

    while (remaining > 0) {
        size_t len = min_t(size_t, remaining,
                           LCD_CHUNK_SIZE);

        ret = ili9341_data(lcd, buf, len);
        if (ret)
            goto out;

        remaining -= len;
    }

out:
    kfree(buf);
    return ret;
}

static int ili9341_probe(struct spi_device *spi)
{
    struct ili9341 *lcd;
    int ret;

    dev_info(&spi->dev, "ILI9341 probe started\n");

    lcd = devm_kzalloc(&spi->dev, sizeof(*lcd),
                       GFP_KERNEL);
    if (!lcd)
        return -ENOMEM;

    lcd->spi = spi;

    lcd->dc = devm_gpiod_get(&spi->dev, "dc",
                             GPIOD_OUT_LOW);
    if (IS_ERR(lcd->dc))
        return dev_err_probe(&spi->dev,
                             PTR_ERR(lcd->dc),
                             "Failed to get DC GPIO\n");

    lcd->reset = devm_gpiod_get(&spi->dev, "reset",
                                GPIOD_OUT_LOW);
    if (IS_ERR(lcd->reset))
        return dev_err_probe(&spi->dev,
                             PTR_ERR(lcd->reset),
                             "Failed to get RESET GPIO\n");

    spi_set_drvdata(spi, lcd);

    ret = ili9341_init(lcd);
    if (ret)
        return dev_err_probe(&spi->dev, ret,
                             "LCD initialization failed\n");

    ret = ili9341_fill_red(lcd);
    if (ret)
        return dev_err_probe(&spi->dev, ret,
                             "Failed to fill screen\n");

    dev_info(&spi->dev,
             "LCD initialized, red screen written\n");


    /* Fill entire screen black */
    ret = ili9341_fill_color(lcd, 0, 0, 240, 320, 0x0000);
    if (ret)
        return ret;

    /* Red rectangle */
    ret = ili9341_fill_color(lcd, 20, 30, 100, 50, 0xF800);
    if (ret)
        return ret;

    /* Green rectangle */
    ret = ili9341_fill_color(lcd, 50, 120, 100, 50, 0x07E0);
    if (ret)
        return ret;

    /* Blue rectangle */
    ret = ili9341_fill_color(lcd, 80, 210, 100, 50, 0x001F);
    if (ret)
        return ret;

    return 0;
}

static void ili9341_remove(struct spi_device *spi)
{
    dev_info(&spi->dev, "ILI9341 driver removed\n");
}

static const struct of_device_id ili9341_of_match[] = {
    { .compatible = "learning,ili9341-lcd" },
    { }
};

MODULE_DEVICE_TABLE(of, ili9341_of_match);

static struct spi_driver ili9341_driver = {
    .driver = {
        .name = "ili9341_lab",
        .of_match_table = ili9341_of_match,
    },
    .probe = ili9341_probe,
    .remove = ili9341_remove,
};

module_spi_driver(ili9341_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Minimal ILI9341 SPI LCD driver");
MODULE_AUTHOR("mizu0312");
