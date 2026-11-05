# Brings the Lua UI packs beside the executable up to date with the source's (cmake -P, SRC and DST the two ui
# folders): a file is copied only when its contents differ, and a file no longer in a shipped pack is removed. Other
# folders under DST (a user's own pack, the packs' settings) are left alone.
#
# Each build used to delete the shipped packs and copy them again, so every script got a new modification time and
# the running client's script scan (ui/lua/lua_host.cpp) reloaded all of them at once: a 170-480 ms frame after each
# rebuild. Now only the scripts that changed are reloaded.

if(NOT DEFINED SRC OR NOT DEFINED DST)
  message(FATAL_ERROR "SyncUiPacks.cmake: set SRC and DST")
endif()

file(GLOB packs LIST_DIRECTORIES true RELATIVE ${SRC} ${SRC}/*)
foreach(pack IN LISTS packs)
  if(NOT IS_DIRECTORY ${SRC}/${pack})
    file(COPY_FILE ${SRC}/${pack} ${DST}/${pack} ONLY_IF_DIFFERENT)
    continue()
  endif()
  file(GLOB_RECURSE wanted RELATIVE ${SRC}/${pack} ${SRC}/${pack}/*)
  foreach(f IN LISTS wanted)
    get_filename_component(dir ${DST}/${pack}/${f} DIRECTORY)
    file(MAKE_DIRECTORY ${dir})
    file(COPY_FILE ${SRC}/${pack}/${f} ${DST}/${pack}/${f} ONLY_IF_DIFFERENT)
  endforeach()
  file(GLOB_RECURSE present RELATIVE ${DST}/${pack} ${DST}/${pack}/*)
  foreach(f IN LISTS present)
    if(NOT EXISTS ${SRC}/${pack}/${f})
      file(REMOVE ${DST}/${pack}/${f})
    endif()
  endforeach()
endforeach()
