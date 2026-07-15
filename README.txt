This program writes to the analog PWM generation PCB over UDP to the KASM_interface_controller.
The port number is currently hardcoded to 5001. It uses a specific protocol defining the command
as specified by protocol.h 

It uses command line options:
    -i : the IP address of the UDP server
    -p : the port that the UPD server is listening to 
    -b : begin, the starting DAC code
    -e : end, the ending DAC code
    -s : step, the number of steps between the start and ending DAC values
    -t : the time to wait between steps
