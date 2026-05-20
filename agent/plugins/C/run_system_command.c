/*
 * ipc_echo.c
 *
 * IPC via stdin/stdout pipe.
 * Reads a newline-terminated string from stdin, echoes it back to stdout.
 *
 * Build:  gcc -o ipc_echo ipc_echo.c
 * Usage:  echo "hello" | ./ipc_echo

 IMPLEMENT LINUX DIRECT SYSTEMCALLS LATER 
 */

#include <stdio.h>
#include <string.h>

#define MAX_BUF 4096

int main(void)
{
    char buf[MAX_BUF];
    char buffer[128];

        
    

		/* Read one line from stdin (Python writes the string + '\n') */
		if (fgets(buf, sizeof(buf), stdin) == NULL) {
			fprintf(stderr, "[C] Error: failed to read from stdin\n");
			return 1;
		}

		/* Strip the trailing newline, if present */
		size_t len = strlen(buf);
		if (len > 0 && buf[len - 1] == '\n') {
			buf[len - 1] = '\0';
			len--;
		}

		fprintf(stderr, "[C] Received  : \"%s\"\n", buf);
		// Open command for reading
		FILE *pipe = popen(buf, "r");
		
		if (pipe) {
			while (fgets(buffer, sizeof(buffer), pipe) != NULL) {
				//printf("Output: %s", buffer);
				/* Echo the string back to stdout followed by a newline so Python's
				 * readline() returns immediately.                                   */
				fprintf(stdout, "%s\n", buffer);
				fflush(stdout);

				fprintf(stderr, "[C] Sent back : \"%s\"\n", buffer);
			}


		pclose(pipe);
    }
    return 0;
}
