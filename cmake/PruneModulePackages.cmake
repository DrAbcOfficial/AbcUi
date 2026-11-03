# Removes stale module packages from the deployment directory: for every
# module id present in the current pack set, older `<id>-<version>.zip` and
# `<id>-<version>.disable` files in the destination are deleted so an
# in-place upgrade cannot leave a duplicated module id behind. Packages whose
# id is not in the current pack set (third-party modules, modules no longer
# built) are left untouched.
#
# Usage:
#   cmake -DSOURCE_PACK_DIR=<dir holding the current module packages>
#         -DDEST_DIR=<deployed abc/abcui/modules directory>
#         -P PruneModulePackages.cmake

if(NOT SOURCE_PACK_DIR OR NOT DEST_DIR)
  message(FATAL_ERROR "PruneModulePackages.cmake needs SOURCE_PACK_DIR and DEST_DIR")
endif()

# Packages ship as "<id>-<version>.zip" (enabled) or "<id>-<version>.disable"
# (the same archive disabled); the extension never changes the identity.
file(GLOB current_packs LIST_DIRECTORIES false
    "${SOURCE_PACK_DIR}/*.zip" "${SOURCE_PACK_DIR}/*.disable")
set(current_names "")
set(current_prefixes "")
foreach(pack_path IN LISTS current_packs)
  get_filename_component(pack_name "${pack_path}" NAME)
  # NAME_WE would cut at the first dot of a dotted id, so strip only the
  # package extension.
  if(NOT pack_name MATCHES "\\.(zip|disable)$")
    continue()
  endif()
  string(REGEX REPLACE "\\.(zip|disable)$" "" pack_name "${pack_name}")
  # `<id>-<version>`: the id itself is dotted, so the id ends at the last dash.
  string(FIND "${pack_name}" "-" split REVERSE)
  if(split LESS 0)
    continue()
  endif()
  string(SUBSTRING "${pack_name}" 0 ${split} pack_id)
  list(APPEND current_names "${pack_name}")
  list(APPEND current_prefixes "${pack_id}")
endforeach()

if(NOT current_prefixes)
  return()
endif()
list(REMOVE_DUPLICATES current_prefixes)

foreach(prefix IN LISTS current_prefixes)
  if(NOT EXISTS "${DEST_DIR}")
    return()
  endif()
  file(GLOB dest_packs LIST_DIRECTORIES false
      "${DEST_DIR}/${prefix}-*.zip" "${DEST_DIR}/${prefix}-*.disable")
  foreach(dest_path IN LISTS dest_packs)
    get_filename_component(dest_name "${dest_path}" NAME)
    string(REGEX REPLACE "\\.(zip|disable)$" "" dest_name "${dest_name}")
    if(dest_name IN_LIST current_names)
      continue()
    endif()
    file(REMOVE "${dest_path}")
    message(STATUS "Pruned stale module package ${dest_name}")
  endforeach()
endforeach()
