package main

import (
	"context"
	"fmt"
	"log"
	"os"
	"os/signal"
	"strings"
	"time"

	"github.com/go-telegram/bot"
	"github.com/go-telegram/bot/models"
)


var (
	botToken   string
	allowedIDs []int64
	deviceName string
	cmdTimeout = 30 * time.Second
)

func init() {
	botToken = "8636020299:AAElccRoqZM8wuh39In0XDDrVSXpWXLQUM8"
	allowedIDs = parseIDs("5769199880")
	deviceName = getEnv("DEVICE_NAME", hostname())
}

// -----------------------------------------------------------------------------

func main() {
	if botToken == "" {
		log.Fatal("BOT_TOKEN environment variable is required")
	}

	ctx, cancel := signal.NotifyContext(context.Background(), os.Interrupt)
	defer cancel()

	opts := []bot.Option{
		bot.WithDefaultHandler(defaultHandler),
		bot.WithErrorsHandler(func(err error) {
			log.Printf("[error] %v", err)
		}),
	}

	b, err := bot.New(botToken, opts...)
	if err != nil {
		log.Fatalf("failed to create bot: %v", err)
	}

	// Register command handlers
	b.RegisterHandler(bot.HandlerTypeMessageText, "/start",    bot.MatchTypeExact,  handleStart)
	b.RegisterHandler(bot.HandlerTypeMessageText, "/help",     bot.MatchTypeExact,  handleHelp)
	b.RegisterHandler(bot.HandlerTypeMessageText, "/info",     bot.MatchTypeExact,  handleInfo)
	b.RegisterHandler(bot.HandlerTypeMessageText, "/run",      bot.MatchTypePrefix, handleRun)
	b.RegisterHandler(bot.HandlerTypeMessageText, "/reboot",   bot.MatchTypeExact,  handleReboot)
	b.RegisterHandler(bot.HandlerTypeMessageText, "/poweroff", bot.MatchTypeExact,  handlePoweroff)

	log.Printf("Bot started on device: %s", deviceName)
	b.Start(ctx)
}

// HANDLERS --------------------------------------------------------------------

func handleStart(ctx context.Context, b *bot.Bot, update *models.Update) {
	if !authorized(update) {
		sendUnauthorized(ctx, b, update)
		return
	}
	text := fmt.Sprintf(
		"👋 *Connected to %s*\n\nUse /help to see available commands\\.",
		bot.EscapeMarkdown(deviceName),
	)
	sendMD(ctx, b, update.Message.Chat.ID, text)
}

func handleHelp(ctx context.Context, b *bot.Bot, update *models.Update) {
	if !authorized(update) {
		sendUnauthorized(ctx, b, update)
		return
	}
	text := `*Available commands*

/info — show device info \(hostname, uptime, CPU, memory\)
/run \<command\> — run a shell command and return output
/reboot — reboot the device
/poweroff — shut down the device

*Examples*
` + "`/run df -h`" + `
` + "`/run cat /proc/cpuinfo`" + `
` + "`/run systemctl status myservice`"

	sendMD(ctx, b, update.Message.Chat.ID, text)
}

func handleInfo(ctx context.Context, b *bot.Bot, update *models.Update) {
	if !authorized(update) {
		sendUnauthorized(ctx, b, update)
		return
	}

	host, _  := runShell(ctx, "hostname", cmdTimeout)
	uptime, _ := runShell(ctx, "uptime -p", cmdTimeout)
	cpu, _    := runShell(ctx, "top -bn1 | grep 'Cpu(s)' | awk '{print $2+$4\"%\"}'", cmdTimeout)
	mem, _    := runShell(ctx, "free -h | awk '/^Mem:/{print $3\"/\"$2}'", cmdTimeout)
	ip, _     := runShell(ctx, "hostname -I | awk '{print $1}'", cmdTimeout)
	temp, _   := runShell(ctx, "vcgencmd measure_temp 2>/dev/null || cat /sys/class/thermal/thermal_zone0/temp 2>/dev/null | awk '{printf \"%.1f°C\", $1/1000}'", cmdTimeout)

	text := fmt.Sprintf(
		"🖥 *Device: %s*\n\n"+
			"🏷 Hostname: `%s`\n"+
			"⏱ Uptime: `%s`\n"+
			"🌐 IP: `%s`\n"+
			"⚙️ CPU: `%s`\n"+
			"💾 Memory: `%s`\n"+
			"🌡 Temp: `%s`",
		bot.EscapeMarkdown(deviceName),
		bot.EscapeMarkdown(strings.TrimSpace(host)),
		bot.EscapeMarkdown(strings.TrimSpace(uptime)),
		bot.EscapeMarkdown(strings.TrimSpace(ip)),
		bot.EscapeMarkdown(strings.TrimSpace(cpu)),
		bot.EscapeMarkdown(strings.TrimSpace(mem)),
		bot.EscapeMarkdown(strings.TrimSpace(temp)),
	)
	sendMD(ctx, b, update.Message.Chat.ID, text)
}

func handleRun(ctx context.Context, b *bot.Bot, update *models.Update) {
	if !authorized(update) {
		sendUnauthorized(ctx, b, update)
		return
	}

	cmd := strings.TrimSpace(strings.TrimPrefix(update.Message.Text, "/run"))
	if cmd == "" {
		sendMD(ctx, b, update.Message.Chat.ID, "⚠️ Usage: `/run <command>`")
		return
	}

	// Send a "running..." indicator first
	sent, sendErr := b.SendMessage(ctx, &bot.SendMessageParams{
		ChatID:    update.Message.Chat.ID,
		Text:      fmt.Sprintf("⏳ Running on *%s*: `%s`", deviceName, cmd),
		ParseMode: "MarkdownV2",
	})

	output, runErr := runShell(ctx, cmd, cmdTimeout)

	result := strings.TrimSpace(output)
	if result == "" {
		result = "(no output)"
	}
	// Telegram message limit is 4096 chars; truncate if needed
	if len(result) > 3800 {
		result = result[:3800] + "\n... (truncated)"
	}

	status := "✅"
	if runErr != nil {
		status = "❌"
	}

	reply := fmt.Sprintf(
		"%s *%s* — `%s`\n\n```\n%s\n```",
		status,
		bot.EscapeMarkdown(deviceName),
		bot.EscapeMarkdown(cmd),
		bot.EscapeMarkdown(result),
	)

	// Edit the "running..." message if we can, otherwise send a new one
	if sendErr == nil && sent != nil {
		_, editErr := b.EditMessageText(ctx, &bot.EditMessageTextParams{
			ChatID:    update.Message.Chat.ID,
			MessageID: sent.ID,
			Text:      reply,
			ParseMode: "MarkdownV2",
		})
		if editErr != nil {
			sendMD(ctx, b, update.Message.Chat.ID, reply)
		}
	} else {
		sendMD(ctx, b, update.Message.Chat.ID, reply)
	}
}

func handleReboot(ctx context.Context, b *bot.Bot, update *models.Update) {
	if !authorized(update) {
		sendUnauthorized(ctx, b, update)
		return
	}
	sendMD(ctx, b, update.Message.Chat.ID,
		fmt.Sprintf("🔄 Rebooting *%s*\\.\\.\\.", bot.EscapeMarkdown(deviceName)))
	runShell(ctx, "sudo reboot", cmdTimeout) //nolint:errcheck
}

func handlePoweroff(ctx context.Context, b *bot.Bot, update *models.Update) {
	if !authorized(update) {
		sendUnauthorized(ctx, b, update)
		return
	}
	sendMD(ctx, b, update.Message.Chat.ID,
		fmt.Sprintf("⏻ Shutting down *%s*\\.\\.\\.", bot.EscapeMarkdown(deviceName)))
	runShell(ctx, "sudo poweroff", cmdTimeout) //nolint:errcheck
}

func defaultHandler(ctx context.Context, b *bot.Bot, update *models.Update) {
	if update.Message == nil {
		return
	}
	if !authorized(update) {
		sendUnauthorized(ctx, b, update)
		return
	}
	sendMD(ctx, b, update.Message.Chat.ID,
		"❓ Unknown command\\. Use /help to see available commands\\.")
}

// HELPERS ---------------------------------------------------------------------

func authorized(update *models.Update) bool {
	if len(allowedIDs) == 0 {
		return true
	}
	if update.Message == nil {
		return false
	}
	chatID := update.Message.Chat.ID
	for _, id := range allowedIDs {
		if id == chatID {
			return true
		}
	}
	return false
}

func sendUnauthorized(ctx context.Context, b *bot.Bot, update *models.Update) {
	if update.Message == nil {
		return
	}
	b.SendMessage(ctx, &bot.SendMessageParams{ //nolint:errcheck
		ChatID: update.Message.Chat.ID,
		Text:   "🚫 Unauthorized. Your chat ID: " + fmt.Sprint(update.Message.Chat.ID),
	})
}

func sendMD(ctx context.Context, b *bot.Bot, chatID any, text string) {
	b.SendMessage(ctx, &bot.SendMessageParams{ //nolint:errcheck
		ChatID:    chatID,
		Text:      text,
		ParseMode: "MarkdownV2",
	})
}

func getEnv(key, fallback string) string {
	if v := os.Getenv(key); v != "" {
		return v
	}
	return fallback
}

func parseIDs(s string) []int64 {
	if s == "" {
		return nil
	}
	var ids []int64
	for _, part := range strings.Split(s, ",") {
		part = strings.TrimSpace(part)
		var id int64
		fmt.Sscanf(part, "%d", &id)
		if id != 0 {
			ids = append(ids, id)
		}
	}
	return ids
}
// Add (string, error) to the function signature to fix the return errors
func ReadFileContent(relativePath string) (string, error) {
	data, err := os.ReadFile(relativePath)
	if err != nil {
		return "", err // Properly handle and return the error
	}

	return string(data), nil
}

func hostname() string {
	// Handle both the string value and the error returned by the function
	content, err := ReadFileContent("../orchestration/machine.txt")
	if err != nil {
		log.Printf("Failed to read hostname file: %v", err)
		return "unknown-host" // Return a fallback default value on error
	}

	return content
}
