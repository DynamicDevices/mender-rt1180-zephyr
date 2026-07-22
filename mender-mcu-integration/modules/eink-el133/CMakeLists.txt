# SPDX-License-Identifier: Apache-2.0
zephyr_library()
zephyr_library_sources_ifdef(CONFIG_EL133UF1 drivers/display/display_el133uf1.c)
zephyr_include_directories(include)
