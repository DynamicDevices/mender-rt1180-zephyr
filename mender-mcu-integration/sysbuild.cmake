# Image-specific overlays for sysbuild (west --sysbuild).
# FRDM MCUboot must not run from HyperRAM: MEMC FLEXSPI_Init of FlexSPI1
# while the relocated NOR driver lives in that window → flash area -ENODEV.
if(BOARD STREQUAL "frdm_imxrt1186")
  set(mcuboot_EXTRA_DTC_OVERLAY_FILE
      "${CMAKE_CURRENT_LIST_DIR}/boards/frdm_imxrt1186_mimxrt1186_cm33_mcuboot_dtcm.overlay"
  )
endif()
