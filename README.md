# fpp-lor-inputpup
LOR Input Pup Plugin for FPP

Polls a LOR "Input Pup" device on a serial port using the standard LOR
heartbeat/poll protocol (the same protocol xSchedule's `ListenerLor` uses) and
triggers FPP Commands based on its button presses.

Requires FPP 10.0 or later (uses the Drogon-based plugin HTTP API).

## Add Plugin URL to FPP
https://raw.githubusercontent.com/computergeek1507/fpp-lor-inputpup/main/pluginInfo.json

## Configuration

On the plugin's settings page, set:

- **Serial Port** - the serial device the Input Pup is connected to (e.g. `ttyUSB0`).
- **Speed** - the baud rate to use (typically 19200 for LOR networks).
- **LOR Unit Id** - the hex unit id the device is configured for (e.g. `0x01`).

The page also shows a live status of the 8 inputs, refreshed every second.

## Events

Each button press/release is reported internally as an event string:

```
LOR:<unitId>:<input>:<state>
```

where `input` is 1-8 and `state` is `1` for pressed, `0` for released. For
example, `LOR:1:3:1` means input 3 on unit `0x01` was just pressed.

Add an Event entry with a Condition (e.g. "Ends With" `:3:1` to match only
input 3 being pressed) and a Command to run when it matches. The "Last
Messages" panel shows recent events, useful for confirming which unit/input
numbers to use.
