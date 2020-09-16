# winrun_svr
The Windows server for winrun (https://github.com/TheMohawkNinja/winrun) to send commands to.

winrun_svr recieves the commands from winrund, executes the commands, and returns the output line-by-line back to winrund for writing to an output file.
This program listens on port 55000 for the TCP handshake.
