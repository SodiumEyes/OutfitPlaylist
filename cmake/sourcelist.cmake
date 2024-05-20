set(sources ${sources}
	src/plugin.cpp
	src/hook.cpp
	src/OutfitPlaylist.cpp

	${NG_UTIL_DIR}/src/ActorUtil.cpp
	${NG_UTIL_DIR}/src/FormIDUtil.cpp
	${NG_UTIL_DIR}/src/MathUtil.cpp
	${NG_UTIL_DIR}/src/StringUtil.cpp

	${JSON_CPP_DIR}/src/lib_json/json_reader.cpp
	${JSON_CPP_DIR}/src/lib_json/json_value.cpp
	${JSON_CPP_DIR}/src/lib_json/json_writer.cpp
)