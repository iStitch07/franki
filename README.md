# FRANKI 

(why franki? because my build and board looks like Dr. Frankenstein's monster)

## Home DIY CO2 sensor based on Senseair S8-0053 and esp8266

### What its can do

Read every 15 seconds current CO2 level from S8-0053 and send it to MQTT broker in json format.  

Info: 
- **current**: current CO2 value from sensor;
- **mean**: mean value of CO2 between current and last measuring with smoothing factor 0.5
- **mean2**: mean value of CO2 between current and last measuring with smoothing factor 0.15

Additional info:
- IP address of device;
- Auto Baseline Calibration (abc) interval;
- Uptime of device;
- Device numeric state;

Example:  
```json
{
    "IP:":"192.168.1.145",
    "abc":180,
    "Uptime":"00:00:11:30",
    "current":553,
    "mean":553,
    "mean2":552,
    "status":0,
    "JsonMemUsage":182,
    "freeHeap":46352
}
```
    
### Remote management

Device can be remote management via MQTT and UDP  
After start, device open and listening 911/udp port.  
At this time only one command is implemented: reboot.  
You can send this command by netcat, for example for MacOS:  
    `echo -n "reboot" | nc -4u -w0 <IP_ADDRESS_OF_DEVICE> 911`

Also, you can start start manual calibrate proccess, by sending message "calibrate" to MQTT topic "esp/set/franki"
    

