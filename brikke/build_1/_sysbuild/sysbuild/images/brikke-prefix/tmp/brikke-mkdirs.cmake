# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file LICENSE.rst or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "C:/Users/ncs-projects/tidtakersystemGIT/brikke")
  file(MAKE_DIRECTORY "C:/Users/ncs-projects/tidtakersystemGIT/brikke")
endif()
file(MAKE_DIRECTORY
  "C:/Users/ncs-projects/tidtakersystemGIT/brikke/build_1/brikke"
  "C:/Users/ncs-projects/tidtakersystemGIT/brikke/build_1/_sysbuild/sysbuild/images/brikke-prefix"
  "C:/Users/ncs-projects/tidtakersystemGIT/brikke/build_1/_sysbuild/sysbuild/images/brikke-prefix/tmp"
  "C:/Users/ncs-projects/tidtakersystemGIT/brikke/build_1/_sysbuild/sysbuild/images/brikke-prefix/src/brikke-stamp"
  "C:/Users/ncs-projects/tidtakersystemGIT/brikke/build_1/_sysbuild/sysbuild/images/brikke-prefix/src"
  "C:/Users/ncs-projects/tidtakersystemGIT/brikke/build_1/_sysbuild/sysbuild/images/brikke-prefix/src/brikke-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "C:/Users/ncs-projects/tidtakersystemGIT/brikke/build_1/_sysbuild/sysbuild/images/brikke-prefix/src/brikke-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "C:/Users/ncs-projects/tidtakersystemGIT/brikke/build_1/_sysbuild/sysbuild/images/brikke-prefix/src/brikke-stamp${cfgdir}") # cfgdir has leading slash
endif()
