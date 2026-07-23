@echo -off
# Shell-only reboot continuation hook.
# Persistent variables use set without -v. set -v is volatile.
# Shell expands environment values as raw script text. Use this lightweight
# variant only when its variables are trusted and have not been modified.

if x%UefiToolsShellRebootActive% eq x then
  echo RebootTestShell: idle; no active test.
  goto Done
endif

if %UefiToolsShellRebootActive% ne 1 then
  echo RebootTestShell: invalid active state; refusing to reset.
  goto Done
endif

if x%UefiToolsShellRebootTarget% eq x then
  echo RebootTestShell: target is missing; refusing to reset.
  goto Done
endif
if not IsInt(%UefiToolsShellRebootTarget%) then
  echo RebootTestShell: target is invalid; refusing to reset.
  goto Done
endif
if %UefiToolsShellRebootTarget% lt 1 or %UefiToolsShellRebootTarget% gt 20 then
  echo RebootTestShell: target is out of range; refusing to reset.
  goto Done
endif

if x%UefiToolsShellRebootCount% eq x then
  echo RebootTestShell: count is missing; refusing to reset.
  goto Done
endif
if not IsInt(%UefiToolsShellRebootCount%) then
  echo RebootTestShell: count is invalid; refusing to reset.
  goto Done
endif
if %UefiToolsShellRebootCount% lt 1 or %UefiToolsShellRebootCount% gt 20 then
  echo RebootTestShell: count is out of range; refusing to reset.
  goto Done
endif

if /i %UefiToolsShellRebootType% ne warm and %UefiToolsShellRebootType% ne cold then
  echo RebootTestShell: reset type is invalid; refusing to reset.
  goto Done
endif

if %UefiToolsShellRebootCount% ge %UefiToolsShellRebootTarget% then
  goto Complete
endif

# UEFI Shell has comparisons but no portable arithmetic command. Use an
# explicit finite state table for the expected numeric state values.
if %UefiToolsShellRebootCount% eq 1 then
  set -v UefiToolsShellRebootNext 2
  goto PersistNext
endif
if %UefiToolsShellRebootCount% eq 2 then
  set -v UefiToolsShellRebootNext 3
  goto PersistNext
endif
if %UefiToolsShellRebootCount% eq 3 then
  set -v UefiToolsShellRebootNext 4
  goto PersistNext
endif
if %UefiToolsShellRebootCount% eq 4 then
  set -v UefiToolsShellRebootNext 5
  goto PersistNext
endif
if %UefiToolsShellRebootCount% eq 5 then
  set -v UefiToolsShellRebootNext 6
  goto PersistNext
endif
if %UefiToolsShellRebootCount% eq 6 then
  set -v UefiToolsShellRebootNext 7
  goto PersistNext
endif
if %UefiToolsShellRebootCount% eq 7 then
  set -v UefiToolsShellRebootNext 8
  goto PersistNext
endif
if %UefiToolsShellRebootCount% eq 8 then
  set -v UefiToolsShellRebootNext 9
  goto PersistNext
endif
if %UefiToolsShellRebootCount% eq 9 then
  set -v UefiToolsShellRebootNext 10
  goto PersistNext
endif
if %UefiToolsShellRebootCount% eq 10 then
  set -v UefiToolsShellRebootNext 11
  goto PersistNext
endif
if %UefiToolsShellRebootCount% eq 11 then
  set -v UefiToolsShellRebootNext 12
  goto PersistNext
endif
if %UefiToolsShellRebootCount% eq 12 then
  set -v UefiToolsShellRebootNext 13
  goto PersistNext
endif
if %UefiToolsShellRebootCount% eq 13 then
  set -v UefiToolsShellRebootNext 14
  goto PersistNext
endif
if %UefiToolsShellRebootCount% eq 14 then
  set -v UefiToolsShellRebootNext 15
  goto PersistNext
endif
if %UefiToolsShellRebootCount% eq 15 then
  set -v UefiToolsShellRebootNext 16
  goto PersistNext
endif
if %UefiToolsShellRebootCount% eq 16 then
  set -v UefiToolsShellRebootNext 17
  goto PersistNext
endif
if %UefiToolsShellRebootCount% eq 17 then
  set -v UefiToolsShellRebootNext 18
  goto PersistNext
endif
if %UefiToolsShellRebootCount% eq 18 then
  set -v UefiToolsShellRebootNext 19
  goto PersistNext
endif
if %UefiToolsShellRebootCount% eq 19 then
  set -v UefiToolsShellRebootNext 20
  goto PersistNext
endif

echo RebootTestShell: count has no next state; refusing to reset.
goto Done

:PersistNext
set UefiToolsShellRebootCount %UefiToolsShellRebootNext%
if %UefiToolsShellRebootCount% ne %UefiToolsShellRebootNext% then
  echo RebootTestShell: count readback failed; refusing to reset.
  goto Done
endif

echo RebootTestShell: next cycle %UefiToolsShellRebootCount%/%UefiToolsShellRebootTarget% state persisted.
echo RebootTestShell: reset in 3 seconds. Rename startup.nsh to recover.
stall 3000000

if /i %UefiToolsShellRebootType% eq warm then
  reset -w "UefiToolsPkg shell-only reboot test"
else
  reset -c "UefiToolsPkg shell-only reboot test"
endif

# ResetSystem() should not return. Disable first if it does.
echo RebootTestShell: reset returned unexpectedly; disabling the test.
set -d UefiToolsShellRebootActive
if not x%UefiToolsShellRebootActive% eq x then
  echo RebootTestShell: WARNING: failed to clear active state. Rename startup.nsh before rebooting.
endif
goto Done

:Complete
echo RebootTestShell: NORMAL-PATH PASS; returned after reset request %UefiToolsShellRebootCount%/%UefiToolsShellRebootTarget%.
# Clear Active first. Even if later cleanup fails, no further reset is requested.
set -d UefiToolsShellRebootActive
if not x%UefiToolsShellRebootActive% eq x then
  echo RebootTestShell: WARNING: failed to clear active state; no reset requested.
  goto Done
endif
set -d UefiToolsShellRebootCount
set -d UefiToolsShellRebootTarget
set -d UefiToolsShellRebootType

:Done
