# idf_component_register(SRCS "sdcloud_final.c"
#                     INCLUDE_DIRS ".")

# main/CMakeLists.txt
idf_component_register(
    SRCS
        "sdcloud_final.c"   # your future main file
        "spiffs.c"
    INCLUDE_DIRS "."
)
