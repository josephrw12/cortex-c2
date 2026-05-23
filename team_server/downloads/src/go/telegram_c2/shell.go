package main

import (
	"bytes"
	"context"
	"os/exec"
	"time"
)

// runShell executes a shell command with a timeout and returns combined stdout+stderr output.
func runShell(ctx context.Context, command string, timeout time.Duration) (string, error) {
	ctx, cancel := context.WithTimeout(ctx, timeout)
	defer cancel()

	cmd := exec.CommandContext(ctx, "bash", "-c", command)

	var out bytes.Buffer
	cmd.Stdout = &out
	cmd.Stderr = &out

	err := cmd.Run()
	return out.String(), err
}
