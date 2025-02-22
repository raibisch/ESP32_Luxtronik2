
# LUXTRONIK HEATPUMP WEBSERVICE WEB-APP

LUXTRONIK Heatpump Controller Interface based on a Websocket (for Heatpumps from alpha-inotec, Novelan, Nibe, Bosch,..)
List and calculate most of the values from Luxtronik 
(this Version has been developedand tested based on software version V3.90.4, not testet with V2.x, does not work with V1.x)

.. a wild mix of german and (in source code) english.
(if it is usefull for other users there is no problem to communictate at ISSUES or PULL REQUESTS in english)


## Add-Ons:

- Tibber-Puls Adapter Integration
- SGready (Smart-Grid-Ready Relay-Interface) with Hardware or Tasmota or Shelly Relais
- Shelly HT3 indoor room temperatur sensor via MQTT
- API and visualisation of EPEX-Price Data including local fix costs and tax
- RS485 Modbus integration for DS100 Energy-Meter (needs extra hardware)

## Highlights:

- online COP calculation (actual, day and sum)
- some extra calculation for delta temperature values
- use HTTP-Get: '/fetch' and '/fetchmeter' for getting values for home-automation programs 


## todo:

- Add webclient-protocol for temp. +/+ 5Kelvin auto/manual temp. correction based on indoor room temperature (because not possible with WebSocket communication)

## Screenshots of Web-App ...runs on every Webbrowser (mobil or PC):

#### Start-Page
![index](/pict/lux_index.png)  

#### Luxtronik detail data
![details](/pict/lux_details.png)  

#### Actual Day-Ahead Price inc. fix-cost and tax (optional)
![epex](/pict/lux_epex.png)  

#### View and set (manual or based on EPEX-Values) the SG-ready Interface of Heatpump (optional)
![epex](/pict/lux_sgready.png)  


#### View Tibber-Pulse or Tasmota Meter values (optional) and DS100 Meter values (optional)
![epex](/pict/lux_meter.png)  

#### Text based Config-File
![epex](/pict/lux_config.png)

#### Update Software and Data from Webbrowser (OTG)
![epex](/pict/lux_otg.png)









