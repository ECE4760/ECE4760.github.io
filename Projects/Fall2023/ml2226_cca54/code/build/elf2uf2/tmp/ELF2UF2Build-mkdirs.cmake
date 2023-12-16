# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "/Users/mikeyliang/Pico/pico-sdk/tools/elf2uf2"
  "/Users/mikeyliang/Documents/School/classes/ece4760/ece4760finalproject/build/elf2uf2"
  "/Users/mikeyliang/Documents/School/classes/ece4760/ece4760finalproject/build/elf2uf2"
  "/Users/mikeyliang/Documents/School/classes/ece4760/ece4760finalproject/build/elf2uf2/tmp"
  "/Users/mikeyliang/Documents/School/classes/ece4760/ece4760finalproject/build/elf2uf2/src/ELF2UF2Build-stamp"
  "/Users/mikeyliang/Documents/School/classes/ece4760/ece4760finalproject/build/elf2uf2/src"
  "/Users/mikeyliang/Documents/School/classes/ece4760/ece4760finalproject/build/elf2uf2/src/ELF2UF2Build-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/Users/mikeyliang/Documents/School/classes/ece4760/ece4760finalproject/build/elf2uf2/src/ELF2UF2Build-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/Users/mikeyliang/Documents/School/classes/ece4760/ece4760finalproject/build/elf2uf2/src/ELF2UF2Build-stamp${cfgdir}") # cfgdir has leading slash
endif()
