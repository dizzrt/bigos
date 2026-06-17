## ADDED Requirements

### Requirement: Shell fd isolation after failure
The BigOS shell SHALL preserve its parent-loop standard input, output, and error descriptors after failed redirection, failed pipe setup, failed fork, failed exec, unsupported syntax, or child command failure.

#### Scenario: Failed output redirection does not break shell stdout
- **WHEN** a command with output redirection fails before the child command runs
- **THEN** the shell reports the error, returns to the prompt or next command, and later output still appears on the original stdout

#### Scenario: Failed input redirection does not break shell stdin
- **WHEN** a command with input redirection fails because the input path cannot be opened
- **THEN** the shell reports the error, keeps the original stdin usable, and can read the next command

### Requirement: Shell single-pipe status and close behavior
The BigOS shell SHALL keep single-stage pipe support bounded and deterministic, closing unused pipe endpoints in all participating processes and reporting the right-side command status as the pipe command status.

#### Scenario: Pipe writer EOF reaches reader
- **WHEN** the left command exits and all write ends are closed
- **THEN** the right command observes EOF after consuming the pipe contents

#### Scenario: Pipe status follows right command
- **WHEN** both sides of a single-stage pipe complete
- **THEN** the shell records the bounded status of the right-side command as the pipeline status

### Requirement: Shell bounded POSIX-like surface diagnostic consistency
The BigOS shell SHALL produce deterministic diagnostics and bounded statuses for command-not-found, exec failure, unsupported syntax, parse errors, and external command non-zero exits.

#### Scenario: Command not found status
- **WHEN** a command without a slash cannot be found through the bounded PATH lookup
- **THEN** the shell reports a command-not-found diagnostic and records status 127

#### Scenario: Unsupported syntax status
- **WHEN** a command line contains syntax outside the supported bounded shell grammar
- **THEN** the shell reports unsupported syntax and records status 2 without partially executing the command
