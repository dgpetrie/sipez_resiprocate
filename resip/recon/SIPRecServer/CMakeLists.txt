
set(INCLUDES
   ActiveCallInfo.hxx
   AppSubsystem.hxx
   ConfigParser.hxx
   HttpBase.hxx
   HttpConnection.hxx
   Server.hxx
   SessionManager.hxx
   WebAdmin.hxx
   WebAdminThread.hxx
)

add_executable(SIPRecServer
   AppSubsystem.cxx
   ConfigParser.cxx
   HttpBase.cxx
   HttpConnection.cxx
   SIPRecServer.cxx
   Server.cxx
   SessionManager.cxx
   WebAdmin.cxx
   WebAdminThread.cxx
   ${INCLUDES}
)

set(CONFIG_FILES
   ${CMAKE_SOURCE_DIR}/resip/recon/SIPRecServer/SIPRecServer.config
)

add_custom_command ( TARGET SIPRecServer POST_BUILD
   COMMAND ${CMAKE_COMMAND} -E copy_if_different
   ${CONFIG_FILES} ${CMAKE_BINARY_DIR}/resip/recon/SIPRecServer
   COMMAND_EXPAND_LISTS
)

target_link_libraries(SIPRecServer PUBLIC recon)
if(WIN32)
   if(USE_SIPXTAPI)
     if(SIPXTAPI_PROJS_IN_VS_GUI)
       add_dependencies(SIPRecServer ${SIPXTAPI_VSPROJ_DEPS})
     endif()
     add_custom_command ( TARGET SIPRecServer POST_BUILD
       COMMAND ${CMAKE_COMMAND} -E copy_if_different
       $<TARGET_FILE:SIPXTAPI_CODEC_PLUGINS> $<TARGET_FILE_DIR:SIPRecServer>
       COMMAND_EXPAND_LISTS
     )
   endif()
   if(WITH_SSL)
    add_custom_command ( TARGET SIPRecServer POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
      $<TARGET_FILE:OPENSSL> $<TARGET_FILE_DIR:SIPRecServer>
      COMMAND_EXPAND_LISTS
    )
  endif()
endif()

install(TARGETS SIPRecServer DESTINATION ${CMAKE_INSTALL_SBINDIR})

set_target_properties(SIPRecServer PROPERTIES FOLDER recon/Apps)

