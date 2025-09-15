# idf_component_register(SRCS "compression.c" "heartbeat.c" "sdcloud_final.c"
#                     INCLUDE_DIRS ".")

# main/CMakeLists.txt
idf_component_register(
    SRCS
        "sdcloud_final.c"   # your future main file
        "spiffs.c"
        "heartbeat.c"
    INCLUDE_DIRS "."
)
