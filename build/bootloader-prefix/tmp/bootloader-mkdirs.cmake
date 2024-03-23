# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "/home/fred/esp/master/esp-idf/components/bootloader/subproject"
  "/mnt/nfs/fred/projets/sensors/esp32h2-helloworld/build/bootloader"
  "/mnt/nfs/fred/projets/sensors/esp32h2-helloworld/build/bootloader-prefix"
  "/mnt/nfs/fred/projets/sensors/esp32h2-helloworld/build/bootloader-prefix/tmp"
  "/mnt/nfs/fred/projets/sensors/esp32h2-helloworld/build/bootloader-prefix/src/bootloader-stamp"
  "/mnt/nfs/fred/projets/sensors/esp32h2-helloworld/build/bootloader-prefix/src"
  "/mnt/nfs/fred/projets/sensors/esp32h2-helloworld/build/bootloader-prefix/src/bootloader-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/mnt/nfs/fred/projets/sensors/esp32h2-helloworld/build/bootloader-prefix/src/bootloader-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/mnt/nfs/fred/projets/sensors/esp32h2-helloworld/build/bootloader-prefix/src/bootloader-stamp${cfgdir}") # cfgdir has leading slash
endif()
