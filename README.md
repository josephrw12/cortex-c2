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
 - Fallback Telegram C2

## Features
- Modular and extensible
- Custom JSON database
- Custom DB communication protocol at the application Layer
- Team Server (Windows / Linux / Mac) - But you will have to cross compile the binaries in the ./team_server downloads folder for Linux if you are on Mac or Windows 
- The agent and db run only on Linux
- Supports using Telegram as a C2 (Read the README.md file a ./team_server/downloads/src/go/telegram_c2/README.md)

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

### Automated
```bash
chmod +x ./build.sh
chmod +x ./build_db.sh
chmod +x run.sh

./build.sh
./build_db.sh
./run.sh
```

## Production Usage
 - after running the 2 build scripts deply only the dist folder onto the target environment
 -  the db server and the team server and the team server client msut run on the attacker infrastructure
 -  Set the variables in the ./dist/orchestartion/config.py according to your setup

### Running commands
 - Enter a regular linux command into the team server client
 - For Lateral Movement

 - The agent is purposely built to stop the same command from executing over and over again, so if you issue a command like plugin_download:rpibot and you see an error but you wish to run that command again then run plugin_download:rpibot-somerandom-text  and following that run plugin_download:rpibot once again 
   ```
   # Edit the usernames.txt and passwords.txt file in the ./dist/orchestration folder
   lateral_movement:../plugins/go/lateral_movement/main:-host:<target IP>:-port:<SSH Server PORT>:-delay:500ms
   # Once the command has been run on the target
   cat lateral_output.txt

   ```
 - TO download plugins on demand (Thanks to for the idea: https://sabotagesec.com/)
 - That is the benign plugin you can compile and place your own pluigns in the ./team_server/downloads folder
```
plugin_download:plugin_v1.bin

# or any other binary you download and place in the downloads folder for example the telegram c2 bot
plugin_download:rpibot

# to run the on demand plugins
plugin_run:<Your plugin>
# example
plugin_run:rpibot
```  
## Extend 
- To extend the framework edit the python files in the ./agent/orchestration folder for example to include a Discord C2

## Todo
- Implement plugins for container service exploitation
- Hopefully make it cloud native sometime in the future

## Important
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




