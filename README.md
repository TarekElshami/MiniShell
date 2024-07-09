# Description
This project involves creating a MiniShell, a command-line interpreter that reads commands from the standard input and executes them. Key features include:

- Execution of commands separated by |.
- Input redirection (<), output redirection (>), and error redirection (>&).
- Background execution of commands ending with &.
- Built-in commands like cd, jobs, fg, exit, and umask.
- Handling of SIGINT signals without terminating the MiniShell.

# Team Members

| Name                        | GitHub Nickname              |
|-----------------------|------------------------------|
| Tarek Elshami Ahmed   | [@TarekElshami](https://github.com/TarekElshami) |
| Álvaro Serrano Rodrigo | [@AlvaroS3rrano](https://github.com/AlvaroS3rrano) |

# Description of Code
## Command Execution Strategy and Pipe Management
- User input is read from the command line.
- The command is tokenized into a tline structure.
- The command is checked if it is an internal command (cd, jobs, fg, umask, exit) or external.
  - For internal commands: execute the respective function.
  - For external commands: handle pipes if needed, fork child processes, redirect input/output as necessary, and execute the command using execvp.

## Specific Data Structures
**Tjobs**: Manages background processes with fields for process IDs, command line string, and statuses.

## Background Implementation
An array of Tjobs is initialized to manage up to 30 background commands. Each job tracks command execution details and statuses.

## Signal Handling
SIGINT signal is handled to display the prompt or terminate processes as needed.

## Jobs and Fg Implementation
Background jobs are tracked, displayed, and managed. fg brings jobs to the foreground, waiting for their completion.
