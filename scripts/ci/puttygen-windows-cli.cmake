add_executable(bitshell-puttygen-cli
  ${CMAKE_SOURCE_DIR}/cmdgen.c
  ${CMAKE_SOURCE_DIR}/stubs/no-timing.c
  bitshell-puttygen-random.c
  noise.c
  no-jump-list.c
  storage.c
  ${CMAKE_SOURCE_DIR}/sshpubk.c
  ${CMAKE_SOURCE_DIR}/sshrand.c)
target_link_libraries(bitshell-puttygen-cli
  keygen console crypto utils
  ${platform_libraries} bcrypt)
