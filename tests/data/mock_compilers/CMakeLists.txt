add_executable(mock_iccarm mock_iccarm.cpp)

target_link_libraries(mock_iccarm
  PRIVATE
    fmt::fmt
)

add_executable(mock_cl mock_cl.cpp)

target_link_libraries(mock_cl
  PRIVATE
    fmt::fmt
)
