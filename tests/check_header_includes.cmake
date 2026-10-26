# Checks that each public header includes only <stddef.h>, <stdint.h>, <stdbool.h> and other
# public headers, so that a host's compiler needs nothing else to read them.
#
#   cmake -DHEADERS=<header;...> -P check_header_includes.cmake

cmake_minimum_required(VERSION 3.25...4.4)

set(allowed "<stddef.h>" "<stdint.h>" "<stdbool.h>")
foreach(header IN LISTS HEADERS)
    cmake_path(GET header FILENAME name)
    list(APPEND allowed "<${name}>" "\"${name}\"")
endforeach()

set(problems "")
foreach(header IN LISTS HEADERS)
    file(STRINGS "${header}" lines REGEX "^[ \t]*#[ \t]*include")
    foreach(line IN LISTS lines)
        string(REGEX REPLACE "^[ \t]*#[ \t]*include[ \t]*([<\"][^>\"]*[>\"]).*$" "\\1" included
            "${line}"
        )
        if(NOT included IN_LIST allowed)
            string(APPEND problems "\n  ${header}: ${line}")
        endif()
    endforeach()
endforeach()

if(problems)
    message(FATAL_ERROR
        "A public header may include only <stddef.h>, <stdint.h>, <stdbool.h> and other public "
        "headers:${problems}"
    )
endif()
