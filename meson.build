
project('lai', 'c')

include = include_directories('include')

library = static_library('lai',
        'core/eval.c',
        'core/exec.c',
        'core/exec2.c',
        'core/libc.c',
        'core/ns.c',
        'core/opregion.c',
        'core/os_methods.c',
        'helpers/pciroute.c',
        'helpers/resource.c',
        'helpers/sci.c',
        'helpers/sleep.c',
        'core/vsnprintf.c',
    include_directories: include,
    pic: false)

dependency = declare_dependency(link_with: library,
    include_directories: include)

