add_library(gtest STATIC googletest/src/gtest-all.cc)
target_include_directories(gtest SYSTEM PUBLIC googletest/include)
target_include_directories(gtest PRIVATE googletest)

# NOTE(josh): compatible with cmake 3.1.0+
target_link_libraries(gtest Threads::Threads)

# NOTE(josh): compatible with cmake 2.8.12+.
# target_compile_options(gtest PUBLIC "-pthread")
# target_link_libraries(gtest PUBLIC ${CMAKE_THREAD_LIBS_INIT})

add_library(gtest_main STATIC googletest/src/gtest_main.cc)
target_include_directories(gtest_main SYSTEM PUBLIC googletest/include)
target_include_directories(gtest_main PRIVATE googletest)

# NOTE(josh): compatible with cmake 3.1.0+
target_link_libraries(gtest_main Threads::Threads)

# NOTE(josh): compatible with cmake 2.8.12+.
# target_compile_options(gtest_main PUBLIC "-pthread")
# target_link_libraries(gtest_main PUBLIC ${CMAKE_THREAD_LIBS_INIT})

set(re2_sources
    re2/util/rune.cc
	re2/util/strutil.cc
	re2/re2/bitstate.cc
	re2/re2/compile.cc
	re2/re2/dfa.cc
	re2/re2/filtered_re2.cc
	re2/re2/mimics_pcre.cc
	re2/re2/nfa.cc
	re2/re2/onepass.cc
	re2/re2/parse.cc
	re2/re2/perl_groups.cc
	re2/re2/prefilter.cc
	re2/re2/prefilter_tree.cc
	re2/re2/prog.cc
	re2/re2/re2.cc
	re2/re2/regexp.cc
	re2/re2/set.cc
	re2/re2/simplify.cc
	re2/re2/stringpiece.cc
	re2/re2/tostring.cc
	re2/re2/unicode_casefold.cc
	re2/re2/unicode_groups.cc)

add_library(re2 STATIC ${re2_sources})
target_link_libraries(re2 Threads::Threads)