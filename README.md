# bme680fs
A Plan9 (9p) file system for bme680 environmental sensor on I2C bus.

## about

bme680fs provides control for a [bme680](https://www.bosch-sensortec.com/products/environmental-sensors/gas-sensors/bme680/) environmental sensor, connected to a [I2C](https://en.wikipedia.org/wiki/I%C2%B2C) bus.

It provides a file in /srv.

`/srv/bme680fs`

And automatically moutn in the current namespace in /mnt, providing ctl file for control and files for accessing environmental measurements.

`/mnt/bme680/ctl`

`/mnt/bme680/temp`

`/mnt/bme680/pres`

`/mnt/bme680/hum`

`/mnt/bme680/gas`

`/mnt/bme680/all`



The display module used for developing and testing was DFROBOT [SEN0248](https://wiki.dfrobot.com/Gravity__I2C_BME680_Environmental_Sensor__VOC,_Temperature,_Humidity,_Barometer__SKU__SEN0248) connected to i2c1 on Raspberry Pi 1 running 9front with I2C enabled.

## requirements

A computer with I2C IO (eg. Raspberry Pi) running Plan9 or 9front with I2C support enabled/added.

## building

`mk`

## usage

Running bme680fs will start the program and provide a file in /srv. In the namespace it was run, it will also mount ctl and display interface files in /mnt.

Once it has been run, it can be accessed from other namespaces by mounting the srv file

`mount -b /srv/bme680fs /mnt`

The program provides two files for controlling the display:

### bme680/ctl

Control file.

Cating the file will print out the current parameters for running measurements, as described in the datasheet.

`cat /mnt/bme680/ctl`

Echoing parameter name and value (eg.: osrs_t 0x03) will change that parameter value.

`echo osrs_t 0x03 > /mnt/bme680/ctl`


### bme680/...

Cating individual files will execute a measurement with that environmental sensors and display the result

## notes

Gas measurement only supports one heating point.

Gas measurement, currently broken because the test sensor was burned by too frequent gas measurement while testing. This is disabled now in the code with a mandatory timeout (check the code). Will be (probably) fixed when new sensors arrive.

Yes, the code is "ugly", but more readable than the Arduino library provided by BOSCH.

Yes, it can always be better.
