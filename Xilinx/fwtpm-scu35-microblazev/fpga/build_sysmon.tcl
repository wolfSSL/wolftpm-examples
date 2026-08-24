# build_sysmon.tcl
#
# wolfSSL build driver that regenerates the AMD "SCU35 Zephyr RTOS IO" TRD
# device image with the fabric hardware TRNG (SYSMONE4) added by add_sysmon.tcl,
# for the minimal ECC-only MicroBlaze V fwTPM (-DFWTPM_TINY_HWTRNG).
#
# It reuses the AMD TRD's block-design script (config_bd.tcl) and constraints
# unmodified, sourcing them by path; it does not copy or alter any AMD source.
# The firmware ELF is JTAG-loaded at run time, so no software image is embedded.
#
# Usage (from this fpga/ directory):
#   vivado -mode batch -notrace -source build_sysmon.tcl -tclargs \
#       -trd /path/to/scu35-zephyr-rtos-io-trd/hw [-jobs 8]
#
# Output: build_sysmon/scu35_sysmon_wrapper.pdi
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

set script_folder [file dirname [file normalize [info script]]]
set jobs 8
set trd_hw ""

for {set i 0} {$i < $argc} {incr i} {
  switch -- [lindex $argv $i] {
    -jobs { incr i; set jobs [lindex $argv $i] }
    -trd  { incr i; set trd_hw [lindex $argv $i] }
  }
}
if {$trd_hw eq ""} {
  error "Pass -trd /path/to/scu35-zephyr-rtos-io-trd/hw"
}

set proj_name scu35_sysmon
set bd_name   scu35_zephyr_rtos_io_trd
# get_board_parts returns a list. It is empty when the AMD SCU35 board files
# are not installed, and can hold more than one entry when several board
# revisions are present. Resolve it to exactly one part here, so a missing or
# ambiguous board file fails with a clear message instead of surfacing later
# as a confusing create_project / set_property board_part error.
set board_matches [get_board_parts "*:scu35:*" -latest_file_version]
if {[llength $board_matches] == 0} {
  error "No SCU35 board part found. Install the AMD SCU35 board files\
         (Vivado: Tools > Vivado Store > Boards) or point\
         BOARD_PART_REPO_PATHS at them, then re-run this script."
}
if {[llength $board_matches] > 1} {
  puts "WARNING: [llength $board_matches] SCU35 board parts matched:"
  foreach bp $board_matches {
    puts "  $bp"
  }
  puts "WARNING: using the first match; narrow the filter to override."
}
set proj_board [lindex $board_matches 0]
puts "Board Part: $proj_board"

create_project -name ${proj_name} -force -dir ./build_sysmon \
  -part [get_property PART_NAME [get_board_parts $proj_board]]
set_property board_part $proj_board [current_project]

import_files -fileset constrs_1 $trd_hw/xdc/EK_SCU35_05204-01_251124.xdc
import_files -fileset sources_1 $trd_hw/src/fanout_pmod.v
update_compile_order
update_ip_catalog

# Base block design (AMD TRD, unmodified) named to match the wrapper the TRD
# tooling expects; add_sysmon.tcl references cells by that design.
set design_name $bd_name
create_bd_design $bd_name
current_bd_design $bd_name
current_bd_instance [get_bd_cells /]
source $trd_hw/scripts/config_bd.tcl

# wolfSSL overlay: add the SYSMONE4 hardware TRNG (validates + saves the BD).
source $script_folder/add_sysmon.tcl

make_wrapper -files [get_files \
  ./build_sysmon/${proj_name}.srcs/sources_1/bd/$bd_name/${bd_name}.bd] -top
import_files -force -norecurse \
  ./build_sysmon/${proj_name}.srcs/sources_1/bd/$bd_name/hdl/${bd_name}_wrapper.v
update_compile_order
set_property top ${bd_name}_wrapper [current_fileset]
update_compile_order -fileset sources_1
save_bd_design

generate_target all [get_files \
  ./build_sysmon/${proj_name}.srcs/sources_1/bd/$bd_name/${bd_name}.bd]
set_property synth_checkpoint_mode Hierarchical [get_files \
  ./build_sysmon/${proj_name}.srcs/sources_1/bd/$bd_name/${bd_name}.bd]

launch_runs synth_1 -jobs ${jobs}
wait_on_run synth_1

launch_runs impl_1 -to_step write_bitstream -jobs ${jobs}
wait_on_run impl_1

set pdi ./build_sysmon/${proj_name}.runs/impl_1/${bd_name}_wrapper.pdi
if {[file exists $pdi]} {
  file copy -force $pdi ./build_sysmon/scu35_sysmon_wrapper.pdi
  puts "SYSMON PDI: [file normalize ./build_sysmon/scu35_sysmon_wrapper.pdi]"
} else {
  error "Expected PDI not found: $pdi"
}

exit
