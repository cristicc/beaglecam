## Booting Linux on BeagleBone Black in less than 3 seconds

The main purpose of this work is to optimize the Linux kernel boot time, measured using an Arduino Nano 33 BLE board driving an alphanumeric LCD display via I2C.

IMPORTANT: This is work in progress!

### Hardware setup

#### Overview

- Communication

![Hardware overview](img/hardware-overview.svg)

#### Camera module

- Signals

![Camera module signals](img/cam-module-signals.svg)


### Build process

- Buildroot

```shell
$ curl -O https://buildroot.org/downloads/buildroot-${BR_VER}.tar.bz2
$ tar -xf buildroot-${BR_VER}.tar.bz2
$ ln -s buildroot-${BR_VER} buildroot
```
