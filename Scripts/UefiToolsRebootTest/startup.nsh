@echo -off
if exist %0.efi then
  %0.efi reboot continue
else
  echo UefiTools: missing %0.efi; refusing to reset.
endif
