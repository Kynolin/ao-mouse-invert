cmake_minimum_required(VERSION 3.15)
project(ao-mouse-invert C)

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/minhook/src/hook.c")
  message(FATAL_ERROR
    "MinHook source is missing. Run:\n  git submodule update --init\n"
    "or clone with:  git clone --recursive <repo-url>")
endif()

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  set(HDE_SRC third_party/minhook/src/hde/hde64.c)
else()
  set(HDE_SRC third_party/minhook/src/hde/hde32.c)
endif()

add_library(dinput8 SHARED
  src/dinput8.c
  src/dinput8.def
  third_party/minhook/src/buffer.c
  third_party/minhook/src/hook.c
  third_party/minhook/src/trampoline.c
  ${HDE_SRC}
)

target_include_directories(dinput8 PRIVATE third_party/minhook/include)
target_compile_definitions(dinput8 PRIVATE _CRT_SECURE_NO_WARNINGS)
set_target_properties(dinput8 PROPERTIES PREFIX "" OUTPUT_NAME "dinput8")

if(MSVC)
  # Static CRT: no Visual C++ Redistributable needed on the player's machine.
  set_property(TARGET dinput8 PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  target_compile_options(dinput8 PRIVATE /W3)
else()
  target_compile_options(dinput8 PRIVATE -O2 -Wall)
  target_link_options(dinput8 PRIVATE -static-libgcc -s)
  if(CMAKE_SIZEOF_VOID_P EQUAL 4)
    # 32-bit: export undecorated stdcall names (DirectInput8Create, not _DirectInput8Create@20).
    target_link_options(dinput8 PRIVATE -Wl,--kill-at -Wl,--enable-stdcall-fixup)
  endif()
endif()
