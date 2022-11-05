project(linux-dmabuf)
find_package(XCB COMPONENTS XCB XFIXES)
find_package(PkgConfig)
pkg_check_modules(DRM libdrm)
if(NOT DRM_FOUND)
	message(WARNING "libdrm not found, dmabuf soure will be disabled")
	return()
endif()

add_library(linux-dmabuf MODULE dmabuf.c
	../linux-capture/xcursor-xcb.c)
target_include_directories(linux-dmabuf PRIVATE
	"${CMAKE_SOURCE_DIR}/libobs"
	${XCB_INCLUDE_DIRS}
	${DRM_INCLUDE_DIRS})
target_link_libraries(linux-dmabuf libobs glad
	${XCB_LIBRARIES} ${DRM_LIBRARIES} XCB::XCB XCB::XFIXES)
set_target_properties(linux-dmabuf PROPERTIES FOLDER "plugins")
install_obs_plugin(linux-dmabuf)
