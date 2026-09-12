The LOR Input Pup Plugin polls a LOR "Input Pup" device on a serial port using the standard LOR heartbeat/poll protocol and can respond to button presses by invoking FPP Commands.
<p>
Configure the Serial Port, Speed, and LOR Unit Id (hex, e.g. 0x01) that match the Input Pup device's DIP switch settings.
<p>
Each button press/release is reported as an event string in the form <code>LOR:&lt;unitId&gt;:&lt;input&gt;:&lt;state&gt;</code>, where <code>input</code> is 1-8 and <code>state</code> is <code>1</code> for pressed and <code>0</code> for released. For example, <code>LOR:1:3:1</code> means input 3 on unit 0x01 was just pressed.
<p>
For each Event added, the following fields need to be configured:
<p>
<ol>
<li>Description - this is a short description of what the event does.  This is ignored by FPP, but can be used to help you organize the events.</li>
<li>Condition - these are conditions to filter in/out events based on the event string. For example, use "Ends With" with a value of ":3:1" to only respond to input 3 being pressed, or ":3:0" for input 3 being released.</li>
<li>Modifier - optionally transform the matched event string (e.g. a Regex to extract the input number) before it is passed to the Command as %VAL%.</li>
<li>Command - the FPP Command to execute.</li>
</ol>
<p>
The "Last Messages" section in the upper right displays the last 25 events that FPPD has received from the Input Pup device.  Clicking Refresh will refresh the list.  These can be used to help identify which unit/input numbers to use in your conditions.
