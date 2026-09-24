@echo -off
if exist %0.efi then
  %0.efi reboot-test continue
else
  echo UefiTools: missing %0.efi; refusing to reset.
endif
