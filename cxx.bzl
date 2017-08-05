
default_copts = [
    "-Werror=all",
]

def cxx_binary(copts = [], **kwargs):
    return native.cc_binary(
        copts = copts + default_copts,
        **kwargs
    )

def cxx_library(copts = [], **kwargs):
    return native.cc_library(
        copts = copts + default_copts,
        **kwargs
    )

def cxx_test(copts = [], **kwargs):
    return native.cc_test(
        copts = copts + default_copts,
        **kwargs
    )
