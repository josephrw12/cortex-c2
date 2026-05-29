# Important 
- Future updates to this project will be in this folder

# Features in v2:
 - Fully interactive Terminal
 - Can run any command remotely

# Todo
 - Add feature to download plugins on demand
 - Include Database to store history 

# Usage
```bash
# compile the agent
cd agent
gcc -o main main.c  -lwebsockets -lcurl
# Deploy into your target environment
./main

# Start the team server
cd team_server
python3 -m venv venv
source venv/bin/activate
pip install websockets
python3 main.py

# Run the client
cd teacm_server_client
python3 -m http.server

http://127.0.0.1:8000/index.html
```
