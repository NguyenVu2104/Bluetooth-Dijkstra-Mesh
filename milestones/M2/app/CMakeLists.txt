cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(bluetooth_dijkstra)

#M2 thêm file mới
target_sources(app PRIVATE src/main.c src/transport.c)
