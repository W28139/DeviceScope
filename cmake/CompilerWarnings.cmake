# 统一编译警告函数：各目标构建时调用，保证警告级别一致
# MSVC 使用 /W4，GCC/Clang 使用 -Wall -Wextra -Wpedantic
function(devicescope_set_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()