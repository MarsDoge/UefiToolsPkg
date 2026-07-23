@echo -off
if exist %0.efi then
  %0.efi continue
else
  echo RebootTest: missing %0.efi; refusing to reset.
endif
