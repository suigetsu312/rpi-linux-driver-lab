import spidev
import time

spi = spidev.SpiDev()
spi.open(0, 1)              # SPI0, CS1
spi.max_speed_hz = 500000   # 先用 500 kHz
spi.mode = 0

def read_adc(cmd):
    rx = spi.xfer2([cmd, 0x00, 0x00])
    return ((rx[1] << 8) | rx[2]) >> 3

try:
    while True:
        x = read_adc(0xD0)
        y = read_adc(0x90)

        print(f"X={x:4d}, Y={y:4d}")
        time.sleep(0.2)
finally:
    spi.close()
