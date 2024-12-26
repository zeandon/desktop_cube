# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "/root/esp/esp-idf/components/bootloader/subproject"
  "/root/my_esp_project/lvgl/build/bootloader"
  "/root/my_esp_project/lvgl/build/bootloader-prefix"
  "/root/my_esp_project/lvgl/build/bootloader-prefix/tmp"
  "/root/my_esp_project/lvgl/build/bootloader-prefix/src/bootloader-stamp"
  "/root/my_esp_project/lvgl/build/bootloader-prefix/src"
  "/root/my_esp_project/lvgl/build/bootloader-prefix/src/bootloader-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/root/my_esp_project/lvgl/build/bootloader-prefix/src/bootloader-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/root/my_esp_project/lvgl/build/bootloader-prefix/src/bootloader-stamp${cfgdir}") # cfgdir has leading slash
endif()
