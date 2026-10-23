idf_component_register(
    SRCS "nomiq_proto.c" "nomiq_xfer.c" "nomiq_crypto.c" "crypto_mbedtls.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES mbedtls esp_hw_support)
