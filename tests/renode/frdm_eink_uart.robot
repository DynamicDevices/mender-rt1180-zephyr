*** Settings ***
Documentation                 UART smoke of the FRDM-IMXRT1186 e-ink ELF in Renode.
...                           Same ARM ELF as flash. Proof class: renode — not FRDM, not portal, not Spectra 6.
Suite Setup                   Setup
Suite Teardown                Teardown
Test Setup                    Reset Emulation
Test Teardown                 Test Teardown
Resource                      ${RENODEKEYWORDS}

*** Variables ***
${ROOT}                       /data_drive/dd/zephyr-rt1170-eink-spectra6-frdm
${UART}                       sysbus.lpuart1
${REPL}                       @${ROOT}/tests/renode/frdm_imxrt1186.repl
${ELF}                        @${ROOT}/build-frdm-rt1186-eink/mender-mcu-integration/zephyr/zephyr.elf
${VTOR}                       0x14020400
${SP}                         0x3802a828
${PC}                         0x1402d8b4

*** Test Cases ***
Should Print Hello World On LPUART1
    Execute Command           mach create
    Execute Command           machine LoadPlatformDescription ${REPL}
    Execute Command           sysbus LoadELF ${ELF}
    Execute Command           cpu0 VectorTableOffset ${VTOR}
    Execute Command           cpu0 SP ${SP}
    Execute Command           cpu0 PC ${PC}
    Create Terminal Tester    ${UART}
    Start Emulation
    Wait For Line On Uart     Hello World! frdm_imxrt1186    timeout=30
