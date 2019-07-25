
project('lai', 'c')

include = include_directories('include')

library = static_library('lai',
        'core/error.c',
        'core/eval.c',
        'core/exec.c',
        'core/exec-operand.c',
        'core/libc.c',
        'core/ns.c',
        'core/object.c',
        'core/opregion.c',
        'core/os_methods.c',
        'core/variable.c',
        'core/vsnprintf.c',
        'helpers/pci.c',
        'helpers/resource.c',
        'helpers/sci.c',
        'helpers/sleep.c',
        'drivers/ec.c',
    include_directories: include,
    pic: false)

dependency = declare_dependency(link_with: library,
    include_directories: include)

