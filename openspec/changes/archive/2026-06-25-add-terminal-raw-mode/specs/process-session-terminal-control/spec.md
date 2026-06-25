## ADDED Requirements

### Requirement: terminal mode control respects foreground ownership

BigOS SHALL bind terminal mode changes to the existing default terminal foreground process group model. A mode-setting request MUST be accepted only from a permitted foreground/session context or from a documented shell recovery path.

#### Scenario: foreground group may set mode

- **WHEN** a process in the current default terminal foreground group requests a valid terminal mode change
- **THEN** BigOS MUST apply the requested canonical/raw mode to the default terminal
- **AND** subsequent foreground reads MUST observe the new mode

#### Scenario: non-foreground request fails

- **WHEN** a process outside the allowed foreground group or session requests a terminal mode change
- **THEN** BigOS MUST reject the request with deterministic errno
- **AND** the old terminal mode, foreground group binding, and process group membership MUST remain unchanged

#### Scenario: mode query does not require ownership mutation

- **WHEN** a process queries the current default terminal mode
- **THEN** BigOS MUST return a deterministic mode result or deterministic errno
- **AND** the query MUST NOT change foreground group, process group, session, signal, fd, or terminal mode state

### Requirement: foreground lifecycle restores usable terminal mode

BigOS SHALL preserve shell usability when a foreground process group that used raw mode exits, fails to exec, or is reaped. The default terminal MUST have a deterministic path back to canonical mode before normal shell command input resumes.

#### Scenario: shell restores canonical after foreground command

- **WHEN** the shell regains the default terminal foreground group after waiting for a foreground command
- **THEN** the shell or kernel terminal path MUST restore canonical mode before normal shell input continues
- **AND** failure to restore MUST be observable as a deterministic error rather than silent terminal corruption

#### Scenario: recovery exception is canonical-only

- **WHEN** the shell or current session leader uses a documented recovery exception while it is not a member of the current foreground group
- **THEN** BigOS MUST allow only restoration to canonical mode
- **AND** requests to set raw mode through that exception MUST fail deterministically without changing terminal mode

#### Scenario: foreground group invalidation does not leave dangling mode owner

- **WHEN** the terminal foreground group disappears while raw mode is active
- **THEN** BigOS MUST clear or ignore any owner reference associated with that group
- **AND** subsequent mode query or mode set MUST NOT dereference freed process objects

#### Scenario: exec keeps terminal mode

- **WHEN** a foreground process changes terminal mode and then successfully calls `execve`
- **THEN** the default terminal mode MUST remain unchanged across the image replacement
- **AND** the new image MUST NOT gain broader mode-setting permissions than the process already had through foreground/session membership
