# cortex-c2
Cortex C2 is a Open source Linux C2 inspired by the void link C2 framework, and my implementation was built for embedded device security research

# Features
- Modular and extensible
- Custom JSON database
- Custom DB communication protocol at the application Layer
- Team Server (Windows / Linux / Mac)
- The agent runs only on Linux

# Usage
- Run the db_server binary in the ./db folder
- Run the c2_communicator.py in the ./agent/plugins/python folder
- activate the team server in the ./team_server folder
  - activate a python virtual environment
  - pip install falsk falsk-cors
  - python3 app.py
-  open the index.html file in the ./team_server_client folder and issue commands

# Important
- This is work in progress, and I do not know how far this will go, your support is highly appreciated, and I am open to accpeting your contributions to the project
- I used claude ai to help in the build process
- Visit my web site at https://cyberigniter.link


