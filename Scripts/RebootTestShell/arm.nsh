@echo -off
# Arm a finite shell-only reboot test.
# Usage: arm.nsh <1-20> [warm|cold]

if x%1 eq x then
  goto Usage
endif
if not IsInt(%1) then
  goto Usage
endif
if %1 lt 1 or %1 gt 20 then
  goto Usage
endif
if not x%3 eq x then
  goto Usage
endif

if x%2 eq x then
  set -v UefiToolsShellRebootNewType warm
else
  if /i %2 ne warm and %2 ne cold then
    goto Usage
  endif
  set -v UefiToolsShellRebootNewType %2
endif

# Disarm any previous test before replacing its parameters.
if not x%UefiToolsShellRebootActive% eq x then
  set -d UefiToolsShellRebootActive
  if not x%UefiToolsShellRebootActive% eq x then
    echo RebootTestShell: failed to clear the previous active state.
    goto Failed
  endif
endif

set UefiToolsShellRebootTarget %1
if %UefiToolsShellRebootTarget% ne %1 then
  echo RebootTestShell: target readback failed.
  goto Failed
endif

set UefiToolsShellRebootType %UefiToolsShellRebootNewType%
if /i %UefiToolsShellRebootType% ne %UefiToolsShellRebootNewType% then
  echo RebootTestShell: reset type readback failed.
  goto Failed
endif

# Count=1 is persisted before requesting the first reset.
set UefiToolsShellRebootCount 1
if %UefiToolsShellRebootCount% ne 1 then
  echo RebootTestShell: initial count readback failed.
  goto Failed
endif

# Active is written last. Partial setup before this point remains idle.
set UefiToolsShellRebootActive 1
if %UefiToolsShellRebootActive% ne 1 then
  echo RebootTestShell: active-state readback failed.
  goto Failed
endif

echo RebootTestShell: armed for %1 %UefiToolsShellRebootNewType% reset(s).
echo RebootTestShell: first reset in 3 seconds.
stall 3000000

if /i %UefiToolsShellRebootNewType% eq warm then
  reset -w "UefiToolsPkg shell-only reboot test"
else
  reset -c "UefiToolsPkg shell-only reboot test"
endif

echo RebootTestShell: reset returned unexpectedly; disabling the test.
goto Failed

:Usage
echo Usage: arm.nsh ^<1-20^> [warm^|cold]
echo Example: arm.nsh 5 warm
goto Done

:Failed
if not x%UefiToolsShellRebootActive% eq x then
  set -d UefiToolsShellRebootActive
  if not x%UefiToolsShellRebootActive% eq x then
    echo RebootTestShell: WARNING: failed to clear active state. Rename startup.nsh before rebooting.
  endif
endif
echo RebootTestShell: test not started.

:Done
