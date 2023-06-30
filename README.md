# GasMeter Pulse Reader

This project was forked from https://github.com/optoisolated/GasMeter-Pulse-Reader with the intention of providing the correct MQTT topics, such that it will work with the MQTT integration for https://www.home-assistant.io/ with the minimum of fuss.

I use a nodemocuv2 for monitoring at the moment, rather than the hardware created by @MakerMeik and @optoisolated so the pin I use is different (GPIO14 on D5 instead of GPIO9 or GPIO3). If you want to make the hardware, then you'll need to change this.

The mosquitto server is "mosquitto.home" and the device will look for that name using the DNS server provided by the DHCP configuration. The credentials used are stored in your secrets.h file. I add an entry in my DNS server, such that mosquitto.home redirects to the server which is running the mosquitto service.

The homeassistant MQTT integration is configured for the same server and when reloaded should see a new gas_meter device, which provides 2 entities. The gas_consumption entity is suitable for use with the energy dash board. The gas_flow_rate entity I use to determine how much gas is being used at any given moment.

This schematic and PCB layout will allow you to build, read, tally and report on a standard Pulse-Type Gas and Water Meters. Each trigger event consists of 10L of Gas consumed. This device has been confirmed compatible with the following meters:

* Gameco Model 750 Gas Meter (10L/impulse)

This project was forked from MakerMeik's Gasmeter_with_reed project and has been updated to be completely self-contained, self-powered (or direct powered) and has hardware debounce using a Schmitt trigger.

![screenshot](https://github.com/optoisolated/GasMeter-Pulse-Reader/raw/main/Schematic.png)
![screenshot](https://github.com/optoisolated/GasMeter-Pulse-Reader/raw/main/PCB.png)

Here you will find all the files you need to build ESP-01 microcontroller and a reed switch to read your Krom-Schröder gas meter electronically.  

![screenshot](https://github.com/Optoisolated/GasMeter-Pulse-Reader/blob/main/Title.jpg)

## Links
* MakerMeik's original Project video: https://youtu.be/i9hPf0uNzFE
* Thingiverse - 3D-Model of the case: https://www.thingiverse.com/thing:5338232
