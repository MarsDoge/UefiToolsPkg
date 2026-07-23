# Shell-only reboot test

This variant uses only UEFI Shell built-in commands. It does not require
`RebootTest.efi` or any other executable.

The application-backed implementation in `Scripts/RebootTest/` remains the
recommended option for longer or safety-critical testing. Do not deploy both
variants' `startup.nsh` files at the same time.

## Requirements

The target Shell must provide UEFI Shell 2.0 script support and these built-in
commands:

```text
echo  set  if  goto  stall  reset
```

In the edk2 Shell implementation, `set` creates a non-volatile environment
variable by default. `set -v` creates a volatile variable. This test uses four
non-volatile Shell environment variables so progress survives reset:

```text
UefiToolsShellRebootActive
UefiToolsShellRebootTarget
UefiToolsShellRebootCount
UefiToolsShellRebootType
```

## Install

Copy all three scripts to the root of the ESP from which the Shell discovers
`startup.nsh`:

```text
startup.nsh
arm.nsh
stop.nsh
```

Confirm that the Shell shows its `startup.nsh` countdown and executes this
script. Shell startup processing can be disabled by platform policy or skipped
with `Esc`.

## Start

From the same ESP, request 5 warm resets:

```text
arm.nsh 5 warm
```

Cold reset is also supported:

```text
arm.nsh 5 cold
```

The supported range is 1 through 20 resets. The three-second delay is fixed in
the scripts as `stall 3000000`. Arguments must be literal trusted values typed
at the Shell prompt; do not pass untrusted text to `arm.nsh`.

`arm.nsh` removes the old Active state first, writes Target/Type/Count, and
writes Active last. It reads every value back before the first reset.
`startup.nsh` writes and reads back the next Count before every subsequent
reset. Readback is authoritative because the tested edk2 Shell can leave
`lasterror` at zero after some failed `set -d` operations.

UEFI Shell has numeric comparisons but no portable arithmetic command, so
`startup.nsh` uses an explicit finite state table from 1 to 20. Out-of-range,
missing, or ordinary non-numeric state is rejected on the normal parse path.

## Stop and recover

At the Shell prompt:

```text
stop.nsh
```

`stop.nsh` removes Active first. If that fails, it leaves the remaining state
intact and asks you to rename `startup.nsh` before rebooting.

Manual cleanup:

```text
set -d UefiToolsShellRebootActive
set -d UefiToolsShellRebootCount
set -d UefiToolsShellRebootTarget
set -d UefiToolsShellRebootType
```

Offline recovery is to rename or delete `startup.nsh` on the ESP. This is the
first recovery action if a variable update reports an error.

## Safety and limitations

- The automated path is finite and capped at 20 reset requests.
- The next reset-request number is persisted before `reset` is called.
- Missing or out-of-range state is rejected on the normal parse path.
- A failed variable write or deletion is detected by readback and does not
  request the next reset.
- Completion deletes Active before cleaning the remaining variables.
- If `reset` unexpectedly returns, the script attempts to delete Active.
- Non-volatile Shell variables consume firmware variable storage and cause one
  variable update per cycle. Avoid using this variant for long endurance tests.
- Shell environment variables have no structured CRC and provide weaker
  durability/error handling than `RebootTest.efi`.
- Firmware variable behavior and enabled Shell command levels can differ among
  vendor Shell builds. Validate this variant on the target platform before an
  unattended run.
- edk2 Shell performs raw text substitution of environment variables and script
  arguments before parsing the command. A value containing Shell metacharacters
  can alter command syntax instead of reaching the numeric/type checks. The
  variables and `arm.nsh` arguments are therefore trusted input, not a security
  boundary. If state may have been modified or corrupted, rename/delete
  `startup.nsh` before boot and clear the variables offline.
- Count represents the reset request persisted before calling `reset`, not
  proof that this exact call completed. A power loss, manual reset, or abnormal
  interruption after the write but before `reset` can make the final normal-path
  PASS over-count. Use the application-backed implementation when exact result
  integrity matters.

For stronger state validation, filesystem Flush handling, keypress cancellation,
and larger reset counts, use `Scripts/RebootTest/` instead.
