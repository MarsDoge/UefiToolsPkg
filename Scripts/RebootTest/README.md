# RebootTest startup.nsh

`startup.nsh` is the standard script that a UEFI Shell runs automatically after startup. This directory provides a safe startup hook for `RebootTest.efi`.

## Install

Copy the script to the root of the writable test ESP. Copy the built
`RebootTest.efi` to the same root, but name that deployed copy
`startup.nsh.efi`:

```text
startup.nsh
startup.nsh.efi
```

The script uses the UEFI Shell `%0` substitution, which is the full path of the
running script, and executes `%0.efi continue`. This guarantees that the
application comes from the same directory and filesystem as `startup.nsh`; it
does not depend on the current `fsN:` mapping or `PATH`.

The application stores progress in `\RebootTest.state` on that same filesystem. It does not use a UEFI nonvolatile variable, so a long test does not repeatedly update NVRAM.

Do not replace an existing `startup.nsh` without first saving or merging its contents.
Confirm that the Shell displays its `startup.nsh` countdown and runs this
script. A Shell built with startup processing disabled, or one that does not
discover this ESP, cannot continue the test automatically.

## Start a test

From the UEFI Shell, switch to the test ESP and run:

```text
fs0:
startup.nsh.efi start 20 warm 3
```

Arguments:

```text
start <count> [warm|cold] [delay-seconds]
```

- `count` is the number of reset cycles to complete, from 1 to 1000000.
- Reset type defaults to `warm`.
- Delay defaults to 3 seconds and must be from 1 to 60 seconds.
- Press any key during the delay to stop the active test.

`start` writes the initial state and requests the first reset. On each successful return to the UEFI Shell, `startup.nsh` runs:

```text
%0.efi continue
```

The application records one completed cycle before requesting the next reset. After the requested number of cycles, it marks the test inactive, prints `PASS`, and returns to the Shell without resetting again.

## Control and recovery

```text
startup.nsh.efi status
startup.nsh.efi stop
startup.nsh.efi clear
```

- `status` displays the active flag, reset type, completed/target cycles, and delay.
- `stop` keeps the state and disables further resets.
- `clear` deletes `\RebootTest.state` and verifies that it is no longer
  reachable before reporting success.

Safe failure behavior:

- Missing state: the startup hook prints that it is idle and does not reset.
- Corrupt or invalid state: the application refuses to reset.
- State write/flush failure: the application refuses to reset.
- `ResetSystem()` unexpectedly returns: the application attempts to persist an
  inactive state and reports any failure.

If stopping, clearing, or post-`ResetSystem()` cleanup reports a state-write
failure, immediately remove or rename `startup.nsh` before manually rebooting.
To recover without running the application, remove or rename `startup.nsh`, or
delete `\RebootTest.state` from another environment. Removing only the state
file is sufficient to make the startup hook idle.

This test proves that the system repeatedly reached the UEFI Shell startup hook. It does not by itself prove successful OS boot, S3 resume, or complete power removal during a cold reset.
