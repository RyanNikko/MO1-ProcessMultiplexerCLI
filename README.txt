Group 2:
Apa, Giusippi Maria II
Geralde, Klyde Audre Guiguitin
Kho, John Zechariah S.
Taino, Ryan Nicholas A.

Entry Class File: main.cpp (For both project folder and and seperated files) 

*Two variations are available within the repository. The project folder created by Visual Studio Code 2022 & the a copy of the entry class file, config.txt, and csopesy-log.txt*

How to Run Program:

*Either open and run the project folder via Visual Studio 2022 or utilize the entry class and txt files in your own project folder*

1.) To use the program, you must first enter "initialize".
- *Aside from exit, no other commands are available at this point*
2.) After initializing, the program is booted up.
3.) Here are the commands available, case-sensitive:
- exit: Ends the program
- screen -s <name of process>: Creates a process with an associated name. Peforms screen -r command with the name of the process as the parameter, effectively transferring you to a new screen. *Warning: Do not put an empty string.*
- screen -r <name of process>: Go to a specific process with new screen. The new screen's text history and the main
console text history are saved.
- initialize: Can only be used at the start of the program to read the config txt once.
- marquee: Opens the marquee console.
- report-util: screen-ls but appends (or create a txt file and put there) the output into the text file.
- screen-ls: Outputs the current processes status as well as the CPU that currently works for them.
- scheduler-test: Initiates the creation of processes.
- scheduler-stop: Stops the creation of processes.
4.) When using the two screen commands, you will be transported to a new screen, different from the main screen.
The new screen only accepts two commands:
- exit: go back to the main screen
- process-smi: similar to screen-ls, but only the process the user has selected/queried.
