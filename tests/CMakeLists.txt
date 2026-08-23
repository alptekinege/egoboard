find_package(Qt6 6.8 REQUIRED COMPONENTS Core Test Sql)

# Unit tests run against the core library (egoboard_core: Qt Core + Sql only),
# each on its own temporary database file.

function(egoboard_add_test name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE egoboard::core Qt6::Test Qt6::Sql)
    target_compile_options(${name} PRIVATE -Wall -Wextra)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

egoboard_add_test(tst_storage)
egoboard_add_test(tst_sensitive)
egoboard_add_test(tst_bookmarks)
egoboard_add_test(tst_exportimport)
egoboard_add_test(tst_transforms)
egoboard_add_test(tst_snippets)
