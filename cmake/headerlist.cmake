set(headers ${headers}
	src/PCH.h 
	src/log.h
	src/util.h
	src/hook.h 
	src/settings.h
	src/OutfitPlaylist.h
)

include_directories(${JSON_CPP_DIR}/include/)
include_directories(${NG_UTIL_DIR}/include)