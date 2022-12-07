# Hardware

| File | Description |
|---|---|
| `schematic.png` | Full circuit schematic (Altium Designer): ESP32-DevKitC-32D, SCD40, A4988, NEMA 17, 100 µF VMOT capacitor, all powered from a single 5 V rail |
| `alt-lipo-power.png` | Alternative wireless design: 3.7 V Li-Po input with P-MOSFET reverse-current protection (would need a 5 V boost stage) |

## Notes

- **Power:** One 5 V / 3 A adapter powers everything (~1.5 A combined peak). The ESP32 gets 5 V on its `EXT_5V`/`VIN` pin and regulates to 3.3 V internally.
- **A4988 protection:** Put a 100 µF electrolytic capacitor across VMOT and GND, as close to the driver as possible. Without it, LC spikes can go past the A4988's 35 V absolute maximum.
- **A4988 current limit:** Set the potentiometer for your motor before connecting it (see [Pololu's guide](https://www.pololu.com/product/1182)).
- **SLEEP/RESET:** Tie these together on the A4988 so the driver stays enabled.
- **Mechanics:** Our model used the NEMA 17 to turn a lead screw, with a carriage pushing a sliding window pane. `WINDOW_TRAVEL_STEPS` in the firmware sets the travel distance.
