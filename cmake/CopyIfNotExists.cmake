# ============================================================
# 仅当目标文件不存在时复制（用于部署默认 H3CN.toml，避免覆盖用户配置）
#
# 用法：
#   cmake -DSRC=<源文件> -DDST=<目标文件> -P CopyIfNotExists.cmake
# ============================================================

if(NOT DEFINED SRC OR NOT DEFINED DST)
    message(FATAL_ERROR "必须指定 SRC 与 DST")
endif()

if(NOT EXISTS "${SRC}")
    message(FATAL_ERROR "源文件不存在：${SRC}")
endif()

if(EXISTS "${DST}")
    message(STATUS "保留现有配置文件：${DST}")
else()
    file(COPY_FILE "${SRC}" "${DST}")
    message(STATUS "已部署默认配置文件：${DST}")
endif()
