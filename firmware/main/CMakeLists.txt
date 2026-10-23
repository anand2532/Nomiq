idf_component_register(SRCS "main.c" INCLUDE_DIRS "."
    REQUIRES nomiq_proto board radio_sx1262 audio camera storage ui power stt_link)
