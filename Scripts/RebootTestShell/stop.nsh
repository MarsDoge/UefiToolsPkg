@echo -off
# Stop and clear a shell-only reboot test.

# Active must be removed first. Do not touch the remaining state if this fails.
if not x%UefiToolsShellRebootActive% eq x then
  set -d UefiToolsShellRebootActive
  if not x%UefiToolsShellRebootActive% eq x then
    echo RebootTestShell: failed to clear active state.
    echo Rename startup.nsh before rebooting and clear the variables manually.
    goto Done
  endif
endif

if not x%UefiToolsShellRebootCount% eq x then
  set -d UefiToolsShellRebootCount
endif
if not x%UefiToolsShellRebootTarget% eq x then
  set -d UefiToolsShellRebootTarget
endif
if not x%UefiToolsShellRebootType% eq x then
  set -d UefiToolsShellRebootType
endif

echo RebootTestShell: stopped; no further reset will be requested.

:Done
