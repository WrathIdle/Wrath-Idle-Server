if(NOT MOD_PLAYERBOTS_FOUND OR NOT MODULES STREQUAL "static" OR NOT MODULE_MOD-PLAYERBOTS STREQUAL "static")
  message(FATAL_ERROR "WrathIdle requires mod-playerbots and -DMODULES=static. Run tools/wrathidle/Setup-Playerbots.ps1.")
endif()
