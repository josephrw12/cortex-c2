# cortex-c2
Cortex C2 is a Open source Linux post exploitation framework inspired by the void link C2 framework, and my implementation was built for embedded device security research and for educational purposes only.

https://www.sysdig.com/blog/voidlink-threat-analysis-sysdig-discovers-c2-compiled-kernel-rootkits

## Current State
 - It can run any Linux System Command remotely
 - Perform Persistence via a startup process
 - Priviledgge Escalate via CVE 2026-43284 (Thanks to: Mykhailo Stepanov - https://www.linkedin.com/in/mykhailo-stepanov-57857a1a0/)

## Features
- Modular and extensible
- Custom JSON database
- Custom DB communication protocol at the application Layer
- Team Server (Windows / Linux / Mac)
- The agent runs only on Linux

## Usage
### Automated
- ./run.sh

### Manual 
-  compile any C source code files as necessary gcc -g -o <output_binary_name> <c_soruce_file_name>
- Run the db_server binary in the ./db folder
- Edit the DB server IP address in the c2_communicator.py and run it it's located in the ./agent/plugins/python folder
- activate the team server in the ./team_server folder
  - activate a python virtual environment
  - pip install falsk falsk-cors
  - python3 app.py if the DB server runs somewhere else other than on local host then:  TCP_HOST=1<DB IP> TCP_PORT=9100 python3 app.py
-  open the index.html file in the ./team_server_client folder and issue commands (If the team server runs some where other than on the same machine as the client edut the API_URL in the client)

## Todo
- Implement plugins for Privilege escalation, Persistence and for container service exploitation
- Implement a feature to make the plugins available on demand (Thanks to: https://sabotagesec.com/)

## Important
-  Successfully tested on a  Arm Cortex-A53 processor
- My framework is not evasive as the void link framework
- This is work in progress, and I do not know how far this will go, your support is highly appreciated, and I am open to accpeting your contributions to the project (read the contributing.md file)
- I used claude ai to speed up the build process
-  It's named Cortex because I am currently testing it out on a Cortex CPU System 
- Visit my web site at https://cyberigniter.link

## Books 
- The linux programming interface




