file(GLOB_RECURSE SRC_FILES "${CMAKE_CURRENT_SOURCE_DIR}/*.c")
idf_component_register(SRCS ${SRC_FILES}
                    INCLUDE_DIRS "./include/"
					EMBED_TXTFILES "ca_cert.pem"
					EMBED_TXTFILES "dropbox.pem")