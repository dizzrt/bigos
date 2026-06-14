## ADDED Requirements

### Requirement: Shell consumes bounded terminal control input
BigOS shell SHALL consume the default terminal's bounded control-character semantics through stdin and its existing line-input/read-parse-execute loop. The shell MUST handle line end, backspace/delete-like editing feedback, EOF-like input, interrupt-like input, and unsupported control bytes deterministically without requiring termios, sessions, job control, terminal process groups, full POSIX shell language, or dynamic linking.

#### Scenario: EOF-like input has a deterministic shell result
- **WHEN** `/bin/sh` reads EOF-like input from the default terminal while waiting for a command line
- **THEN** shell MUST produce a deterministic bounded result such as ending the current input loop, exiting with a documented status, or reporting a documented no-op
- **AND** the result MUST NOT require POSIX canonical-mode completeness, shell variables, scripts, or terminal process groups

#### Scenario: Interrupt-like input has a deterministic shell result
- **WHEN** `/bin/sh` receives interrupt-like terminal input while reading a command line or waiting for a supported foreground command
- **THEN** shell MUST handle it as a bounded BigOS terminal result such as cancelling the current line, reporting a deterministic message, or documenting no-op behavior
- **AND** the result MUST NOT imply POSIX job control, signal delivery to terminal process groups, sessions, or full foreground/background process semantics

#### Scenario: Unsupported controls do not corrupt shell state
- **WHEN** the default terminal delivers an unsupported control byte or unsupported terminal event to `/bin/sh`
- **THEN** shell MUST ignore it or report deterministic bounded feedback
- **AND** shell MUST return to a valid prompt/read state without corrupting argv parsing, redirection state, pipe state, cwd, or bounded status

### Requirement: Shell terminal feedback remains visible through stdout and stderr
BigOS shell SHALL make prompt text, line-input feedback, control-character feedback, and deterministic shell errors visible through stdout or stderr when connected to the default terminal. The shell MUST NOT directly access hardware output paths or early diagnostic-only APIs for ordinary interactive behavior.

#### Scenario: Prompt and feedback use ordinary output
- **WHEN** shell stdin/stdout/stderr are connected to the default terminal
- **THEN** shell MUST write prompt text and supported line-input feedback through stdout or stderr
- **AND** the resulting output MUST remain compatible with existing fd inheritance, redirection, pipe, and console output boundaries

#### Scenario: Terminal feedback does not replace command output
- **WHEN** shell executes builtins or external commands after terminal input processing
- **THEN** command stdout/stderr MUST remain visible through the ordinary fd/syscall path
- **AND** prompt, echo, or control-character feedback MUST NOT be confused with child process failure, shell crash, or smoke marker output
