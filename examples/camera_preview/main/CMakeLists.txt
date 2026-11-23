append_srcs_dir(ADD_SRCS "src")
list(APPEND ADD_REQUIREMENTS basic vision media_server)
list(APPEND ADD_DEFINITIONS_PRIVATE -std=c++17)
register_component()
