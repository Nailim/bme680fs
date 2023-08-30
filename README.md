# bme680fs
A Plan9 (9p) file system for bme680 environmental sensor on the I2C bus.

## about

bme680fs provides control for a [bme680](https://www.bosch-sensortec.com/products/environmental-sensors/gas-sensors/bme680/) environmental sensor, connected to a [I2C](https://en.wikipedia.org/wiki/I%C2%B2C) bus.

It provides a file in /srv.

`/srv/bme680fs`

And automatically moutn in the current namespace in /mnt, providing a ctl file for control and files for accessing environmental measurements.

`/mnt/bme680/ctl`

`/mnt/bme680/temp`

`/mnt/bme680/pres`

`/mnt/bme680/hum`

`/mnt/bme680/gas`

`/mnt/bme680/all`


The  module used for development and testing was the DFROBOT [SEN0248](https://wiki.dfrobot.com/Gravity__I2C_BME680_Environmental_Sensor__VOC,_Temperature,_Humidity,_Barometer__SKU__SEN0248) connected to i2c1 on a Raspberry Pi 1 running 9front with I2C enabled.

## requirements

A computer with I2C IO (eg. Raspberry Pi) running Plan9 or 9front with I2C support enabled/added.

## building

`mk`

## usage

Running bme680fs will start the program and provide a file in /srv. In the namespace where it was run, it will also mount ctl and display interface files in /mnt.

### starting

The bme680fs can be started by calling the bme680fs binary.

It provides flags to control the behavior:

- -u : set undocumented bits, not mentioned in the datasheet but observed being set when using the BSEC library
- -d : run continuous sampling in a thread that affects the gas measurements (run in the background when starting)

Please reference the startbme680fs.rc script.

### mounting

Once it has been run, it can be accessed from other namespaces by mounting the srv file

`mount -b /srv/bme680fs /mnt`

### controling

The program provides __bme680/ctl__ control file for controlling the sensor:

Cating the file will print out the current parameters for running measurements, as described in the datasheet.

`cat /mnt/bme680/ctl`

No detailed explanation here, if you don't know what they are, don't mess with them.

Echoing the parameter name and value (eg.: osrs_t 0x03) will change that parameter value.

`echo osrs_t 0x03 > /mnt/bme680/ctl`

Additionally, the speed of continuous sampling can be set with the loop_time parameter in ms.

`echo loop_time 1000 > /mnt/bme680/ctl`

### reading values

Cating individual files will execute a measurement with those environmental sensors and display the result

## notes

Gas measurement only supports one heating point.

Gas measurement, currently being investigated further. The measured gas resistance does change depending on air quality (tested with airing the room after a while or breathing on the sensor). But the values are not comparable to the ones acquired with the BSEC Arduino library. The binary blob in that library is doing something extra that is not apparent in the documentation alone.

Yes, the code is "ugly", but more readable than the Arduino library provided by BOSCH.

Yes, it can always be better.
