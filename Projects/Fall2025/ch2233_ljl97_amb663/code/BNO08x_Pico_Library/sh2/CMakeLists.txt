add_library(sh2 STATIC
        src/sh2.c
        src/shtp.c
        src/sh2_util.c
        src/sh2_SensorValue.c
)
target_include_directories(sh2 PUBLIC
        include
)
