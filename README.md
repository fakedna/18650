# 18650 Battery Charger & Tester

Arduino-based 4-channel charger and discharge tester for 18650 Li-ion batteries.

The project uses INA3221 current/voltage monitors to track each channel and measures:

* Charging time
* Discharge capacity (mAh)
* Internal resistance (mΩ)
* Battery voltage and current

A 16×2 LCD displays the current channel status and measurements. Each channel is controlled independently via a MOSFET-based discharge load.
