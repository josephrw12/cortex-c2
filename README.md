# cortex-c2
Cortex C2 is a Open source Linux post exploitation framework inspired by the void link C2 framework, and my implementation was built for embedded device security research and for educational purposes only.

# Current State
 -It can run any Linux System Command remotely 

# Features
- Modular and extensible
- Custom JSON database
- Custom DB communication protocol at the application Layer
- Team Server (Windows / Linux / Mac)
- The agent runs only on Linux

# Usage
-  compile any C source code files as necessary gcc -g -o <output_binary_name> <c_soruce_file_name>
- Run the db_server binary in the ./db folder
- Edit the DB server IP address in the c2_communicator.py and run it it's located in the ./agent/plugins/python folder
- activate the team server in the ./team_server folder
  - activate a python virtual environment
  - pip install falsk falsk-cors
  - python3 app.py if the DB server runs somewhere else other than on local host then:  TCP_HOST=1<DB IP> TCP_PORT=9100 python3 app.py
-  open the index.html file in the ./team_server_client folder and issue commands (If the team server runs some where other than on the same machine as the client edut the API_URL in the client)

# Important
- This is work in progress, and I do not know how far this will go, your support is highly appreciated, and I am open to accpeting your contributions to the project (read the contributing.md file)
- I used claude ai to help in the build process
- Visit my web site at https://cyberigniter.link


