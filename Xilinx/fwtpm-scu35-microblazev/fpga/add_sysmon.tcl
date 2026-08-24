# add_sysmon.tcl
#
# wolfSSL overlay for the AMD "SCU35 Zephyr RTOS IO" Target Reference Design.
# Adds an AXI System Management Wizard (SYSMONE4) as a fabric hardware entropy
# source for the MicroBlaze V fwTPM, so the minimal ECC build can seed its
# Hash-DRBG from the on-die System Monitor instead of wolfCrypt MemUse entropy
# (built with -DFWTPM_TINY_HWTRNG; driver: firmware/fwtpm-mbv/fwtpm_trng_sysmon.c).
#
# Run this AFTER the base block design has been created and is current (i.e.
# after the TRD's config_bd.tcl has been sourced), and BEFORE synthesis. It
# does not modify any AMD source file; it only adds one IP and its connections.
#
# The System Management Wizard is placed in AXI4-Lite mode with the continuous
# sequencer over the on-chip temperature / VCCINT / VCCAUX sensors. Its AXI
# register map exposes those measurements at offsets 0x400 / 0x404 / 0x408
# (DRP address * 4, with the +0x200 System-Management IP offset), which is what
# the firmware driver reads. It is mapped at 0x44A30000 to match
# SCU35_SYSMON_BASE in the firmware.
#
# Copyright (C) 2006-2026 wolfSSL Inc.
#
# This file is part of wolfTPM.
#
# wolfTPM is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# wolfTPM is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA

# 1. Instantiate the System Management Wizard (SYSMONE4), AXI4-Lite interface,
#    continuous sequencer over the on-chip temperature / VCCINT / VCCAUX sensors.
set sysmon [create_bd_cell -type ip \
  -vlnv xilinx.com:ip:system_management_wiz:1.3 system_management_wiz_0]
set_property -dict [list \
  CONFIG.INTERFACE_SELECTION {Enable_AXI} \
  CONFIG.DCLK_FREQUENCY {225} \
  CONFIG.ADC_CONVERSION_RATE {200} \
  CONFIG.SEQUENCER_MODE {Continuous} \
  CONFIG.CHANNEL_ENABLE_TEMPERATURE {true} \
  CONFIG.CHANNEL_ENABLE_VCCINT {true} \
  CONFIG.CHANNEL_ENABLE_VCCAUX {true} \
] $sysmon

# 2. Add one master port to the MicroBlaze V AXI SmartConnect and wire it to the
#    wizard's AXI4-Lite slave. Read the current master count and grow by one so
#    the overlay survives small TRD revisions (the stock TRD has 19).
set sc microblaze_riscv_0_axi_periph
set num_mi [get_property CONFIG.NUM_MI [get_bd_cells $sc]]
set new_mi [format "M%02d_AXI" $num_mi]
set_property CONFIG.NUM_MI [expr {$num_mi + 1}] [get_bd_cells $sc]
connect_bd_intf_net \
  [get_bd_intf_pins $sc/$new_mi] \
  [get_bd_intf_pins system_management_wiz_0/S_AXI_LITE]

# 3. Clock and reset from the MicroBlaze V 225 MHz AXI domain (DCLK is the AXI
#    clock in AXI4-Lite mode; no separate dclk_in port is exposed).
connect_bd_net [get_bd_pins system_management_wiz_0/s_axi_aclk] \
  [get_bd_pins clk_wiz_1/clk_out1]
connect_bd_net [get_bd_pins system_management_wiz_0/s_axi_aresetn] \
  [get_bd_pins rst_clk_wiz_1_100M/peripheral_aresetn]

# 4. Map the wizard at 0x44A30000 (matches SCU35_SYSMON_BASE in the firmware).
assign_bd_address -offset 0x44A30000 -range 0x00010000 \
  -target_address_space [get_bd_addr_spaces microblaze_riscv_0/Data] \
  [get_bd_addr_segs system_management_wiz_0/S_AXI_LITE/Reg] -force

validate_bd_design
save_bd_design
