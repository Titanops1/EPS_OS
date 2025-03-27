# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "/Users/fabianbolte/esp/v5.2/esp-idf/components/bootloader/subproject"
  "/Users/fabianbolte/esp/UDOO_KEY/ESP32/build/bootloader"
  "/Users/fabianbolte/esp/UDOO_KEY/ESP32/build/bootloader-prefix"
  "/Users/fabianbolte/esp/UDOO_KEY/ESP32/build/bootloader-prefix/tmp"
  "/Users/fabianbolte/esp/UDOO_KEY/ESP32/build/bootloader-prefix/src/bootloader-stamp"
  "/Users/fabianbolte/esp/UDOO_KEY/ESP32/build/bootloader-prefix/src"
  "/Users/fabianbolte/esp/UDOO_KEY/ESP32/build/bootloader-prefix/src/bootloader-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/Users/fabianbolte/esp/UDOO_KEY/ESP32/build/bootloader-prefix/src/bootloader-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/Users/fabianbolte/esp/UDOO_KEY/ESP32/build/bootloader-prefix/src/bootloader-stamp${cfgdir}") # cfgdir has leading slash
endif()
