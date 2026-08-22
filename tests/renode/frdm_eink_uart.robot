*** Settings ***
Documentation                 UART smoke of the FRDM-IMXRT1186 e-ink ELF in Renode.
...                           Same ARM ELF as flash. Proof class: renode — not FRDM, not portal, not Spectra 6.
...                           BOM_POWER_LOOP images enter WFI after settle; wrap this suite with `timeout`
...                           or use scripts/renode-frdm-eink-bom-pc-smoke.sh for PC-path proof.
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
${EXPECT_BOM_LOOP}            ${EMPTY}

*** Test Cases ***
Should Boot Eink Elf On LPUART1
    Execute Command           mach create
    Execute Command           machine LoadPlatformDescription ${REPL}
    Execute Command           sysbus LoadELF ${ELF}
    Execute Command           cpu0 VectorTableOffset ${VTOR}
    Execute Command           cpu0 SP ${SP}
    Execute Command           cpu0 PC ${PC}
    Create Terminal Tester    ${UART}    defaultPauseEmulation=true
    Start Emulation
    Wait For Line On Uart     Booting Zephyr OS    timeout=30    pauseEmulation=true
    Wait For Line On Uart     Hello World!    timeout=15    pauseEmulation=true
    IF    '${EXPECT_BOM_LOOP}' == '1'
        Execute Command           start
        Wait For Line On Uart     BOM power-loop: PRE=    timeout=10    pauseEmulation=true
    END
