# Setup Instructions
```bash
# 1. Install Go (see README for ARM64 link)

# 2. Build
cd ~/telegram_c2

# 3. Run
Insert the Bot token and Chat ID in main.go 
func init() {

	botToken = "<BOT TOKEN>"

	allowedIDs = parseIDs("<CHAT ID>")

	...

}

# Run the below commands
go mod tidy
# It must be named as rpibot or else you will get compile time errors
go build -o rpibot .

# In development
./rpibot

# Via the team server (In production)
move the rpibot (or any of the on demand plugins) file to the root fo the downloads folder in the team server
In the team server client
plugin_download:rpibot

plugin_run:rpibot

Check your telegram
```

## Available Commands

| Command           | Description                         |
|-------------------|-------------------------------------|
| `/start`          | Confirm connection                  |
| `/help`           | Show this command list              |
| `/info`           | Show hostname, uptime, CPU, mem, IP |
| `/run <command>`  | Run any shell command               |
| `/reboot`         | Reboot the Pi                       |
| `/poweroff`       | Shut down the Pi                    |

### Examples

```
/run df -h
/run ls /home/pi
/run systemctl status nginx
/run cat /proc/cpuinfo | grep Model
/run ping -c 4 google.com
```

---
