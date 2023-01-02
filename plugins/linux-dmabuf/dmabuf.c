//#include "drmsend.h"
#include "xcursor-xcb.h"

#include <graphics/graphics-internal.h>

// FIXME needed for gl_platform pointer access
#include <../libobs-opengl/gl-subsystem.h>

#include <obs-module.h>
#include <obs-nix-platform.h>
#include <util/platform.h>
#include <glad/glad_egl.h>

#include <sys/wait.h>
#include <stdio.h>
#include <X11/Xutil.h>

#include <fcntl.h>

// FIXME stringify errno

// FIXME integrate into glad
typedef void(APIENTRYP PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)(
	GLenum target, GLeglImageOES image);
GLAPI PFNGLEGLIMAGETARGETTEXTURE2DOESPROC glad_glEGLImageTargetTexture2DOES;
typedef void(APIENTRYP PFNGLEGLIMAGETARGETRENDERBUFFERSTORAGEOESPROC)(
	GLenum target, GLeglImageOES image);
GLAPI PFNGLEGLIMAGETARGETRENDERBUFFERSTORAGEOESPROC
	glad_glEGLImageTargetRenderbufferStorageOES;

// FIXME glad
static PFNGLEGLIMAGETARGETTEXTURE2DOESPROC glEGLImageTargetTexture2DOES;

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>

// FIXME sync w/ gl-x11-egl.c
struct gl_platform {
	Display *xdisplay;
	EGLDisplay edisplay;
	EGLConfig config;
	EGLContext context;
	EGLSurface pbuffer;
};

#include <xf86drm.h>
#include <libdrm/drm_fourcc.h>
#include <xf86drmMode.h>

typedef struct {
	obs_source_t *source;

	xcb_connection_t *xcb;
	xcb_xcursor_t *cursor;
	gs_texture_t *texture;
	EGLDisplay edisp;
	EGLImage eimage;

	bool show_cursor;
	
	// my
	drmModePlanePtr plane;
	uint32_t lastGoodPlane;
	
	int* dma_buf_fd;
	drmModeFB2Ptr fb;
	int drmfd;
	int nplanes;
	uint32_t width;
	uint32_t height;
} dmabuf_source_t;


static EGLImageKHR
create_dmabuf_egl_image(EGLDisplay egl_display, unsigned int width,
			unsigned int height, uint32_t drm_format,
			uint32_t n_planes, const int *fds,
			const uint32_t *strides, const uint32_t *offsets,
			const uint64_t modifier)
{
	EGLAttrib attribs[47];
	int atti = 0;

	/* This requires the Mesa commit in
	 * Mesa 10.3 (08264e5dad4df448e7718e782ad9077902089a07) or
	 * Mesa 10.2.7 (55d28925e6109a4afd61f109e845a8a51bd17652).
	 * Otherwise Mesa closes the fd behind our back and re-importing
	 * will fail.
	 * https://bugs.freedesktop.org/show_bug.cgi?id=76188
	 * */

	attribs[atti++] = EGL_WIDTH;
	attribs[atti++] = width;
	attribs[atti++] = EGL_HEIGHT;
	attribs[atti++] = height;
	attribs[atti++] = EGL_LINUX_DRM_FOURCC_EXT;
	attribs[atti++] = drm_format;

	if (n_planes > 0) {
		attribs[atti++] = EGL_DMA_BUF_PLANE0_FD_EXT;
		attribs[atti++] = fds[0];
		attribs[atti++] = EGL_DMA_BUF_PLANE0_OFFSET_EXT;
		attribs[atti++] = offsets[0];
		attribs[atti++] = EGL_DMA_BUF_PLANE0_PITCH_EXT;
		attribs[atti++] = strides[0];
		if (modifier) {
			attribs[atti++] = EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT;
			attribs[atti++] = modifier & 0xFFFFFFFF;
			attribs[atti++] = EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT;
			attribs[atti++] = modifier >> 32;
		}
	}

	if (n_planes > 1) {
		attribs[atti++] = EGL_DMA_BUF_PLANE1_FD_EXT;
		attribs[atti++] = fds[1];
		attribs[atti++] = EGL_DMA_BUF_PLANE1_OFFSET_EXT;
		attribs[atti++] = offsets[1];
		attribs[atti++] = EGL_DMA_BUF_PLANE1_PITCH_EXT;
		attribs[atti++] = strides[1];
		if (modifier) {
			attribs[atti++] = EGL_DMA_BUF_PLANE1_MODIFIER_LO_EXT;
			attribs[atti++] = modifier & 0xFFFFFFFF;
			attribs[atti++] = EGL_DMA_BUF_PLANE1_MODIFIER_HI_EXT;
			attribs[atti++] = modifier >> 32;
		}
	}

	if (n_planes > 2) {
		attribs[atti++] = EGL_DMA_BUF_PLANE2_FD_EXT;
		attribs[atti++] = fds[2];
		attribs[atti++] = EGL_DMA_BUF_PLANE2_OFFSET_EXT;
		attribs[atti++] = offsets[2];
		attribs[atti++] = EGL_DMA_BUF_PLANE2_PITCH_EXT;
		attribs[atti++] = strides[2];
		if (modifier) {
			attribs[atti++] = EGL_DMA_BUF_PLANE2_MODIFIER_LO_EXT;
			attribs[atti++] = modifier & 0xFFFFFFFF;
			attribs[atti++] = EGL_DMA_BUF_PLANE2_MODIFIER_HI_EXT;
			attribs[atti++] = modifier >> 32;
		}
	}

	if (n_planes > 3) {
		attribs[atti++] = EGL_DMA_BUF_PLANE3_FD_EXT;
		attribs[atti++] = fds[3];
		attribs[atti++] = EGL_DMA_BUF_PLANE3_OFFSET_EXT;
		attribs[atti++] = offsets[3];
		attribs[atti++] = EGL_DMA_BUF_PLANE3_PITCH_EXT;
		attribs[atti++] = strides[3];
		if (modifier) {
			attribs[atti++] = EGL_DMA_BUF_PLANE3_MODIFIER_LO_EXT;
			attribs[atti++] = modifier & 0xFFFFFFFF;
			attribs[atti++] = EGL_DMA_BUF_PLANE3_MODIFIER_HI_EXT;
			attribs[atti++] = modifier >> 32;
		}
	}

	attribs[atti++] = EGL_NONE;

	return eglCreateImage(egl_display, EGL_NO_CONTEXT,
			      EGL_LINUX_DMA_BUF_EXT, 0, attribs);
}


typedef struct {
	int width, height;
	uint32_t fourcc;
	int fd, offset, pitch;
} DmaBuf;

uint32_t lastGoodPlane = 0;

drmModeFB2Ptr prepareImage(int drmfd) {
	
	drmModePlaneResPtr planes = drmModeGetPlaneResources(drmfd);
	
	// Check the first plane (or last good)
	drmModePlanePtr plane = drmModeGetPlane(drmfd, planes->planes[lastGoodPlane]);
	uint32_t fb_id = plane->fb_id;
	drmModeFreePlane(plane);
	
	// Find a good plane
	if (fb_id == 0) {
		for (uint32_t i = 0; i < planes->count_planes; ++i) {
			drmModePlanePtr plane = drmModeGetPlane(drmfd, planes->planes[i]);
			
			if (plane->fb_id != 0) {
				drmModeFB2Ptr fb = drmModeGetFB2(drmfd, plane->fb_id);
				if (fb == NULL) {
					//ctx->lastGoodPlane = 0;
					continue;
				}
				if (fb->handles[0]) {
					if (fb->width == 256 && fb->height == 256)
						continue;
				}
				//drmModeFreeFB2(fb);
				
				lastGoodPlane = i;
				fb_id = plane->fb_id;
				//MSG("%d, %#x", i, fb_id);
				
				drmModeFreePlane(plane);
				return fb;
				break;
			}
			else {
				drmModeFreePlane(plane);
			}
		}
	}
	else {
		return drmModeGetFB2(drmfd, fb_id);
	}
	
	drmModeFreePlaneResources(planes);
	
	//MSG("%#x", fb_id);
	return NULL;
}

void initDmaBufFDs(int drmfd, drmModeFB2Ptr fb, int* dma_buf_fd, int* nplanes) {
	for (int i = 0; i < 4; i++) {
		if (fb->handles[i] == 0) {
			*nplanes = i;
			break;
		}
		drmPrimeHandleToFD(drmfd, fb->handles[i], O_RDONLY, (dma_buf_fd + i));
	}
}

void cleanupDmaBufFDs(drmModeFB2Ptr fb, int* dma_buf_fd, int* nplanes) {
	for (int i = 0; i < *nplanes; i++)
		if (dma_buf_fd[i] >= 0)
			close(dma_buf_fd[i]);
	if (fb)
		drmModeFreeFB2(fb);
}


static void dmabuf_source_close(dmabuf_source_t *ctx)
{
	blog(LOG_DEBUG, "dmabuf_source_close %p", ctx);

	cleanupDmaBufFDs(ctx->fb, ctx->dma_buf_fd, &ctx->nplanes);
	
	if (ctx->eimage != EGL_NO_IMAGE) {
		eglDestroyImage(ctx->edisp, ctx->eimage);
		ctx->eimage = EGL_NO_IMAGE;
	}
}

static void dmabuf_source_open(dmabuf_source_t *ctx)
{
	blog(LOG_DEBUG, "dmabuf_source_open %p %#x", ctx);
	
	const char *card = "/dev/dri/card0";

	blog(LOG_DEBUG, "Opening card %s", card);
	const int drmfd = open(card, O_RDONLY);
	if (drmfd < 0) {
		blog(LOG_DEBUG, "Cannot open card");
		return;
	}
	drmSetClientCap(drmfd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);
	ctx->drmfd = drmfd;
	
	const int available = drmAvailable();
	if (!available)
		return;

	obs_enter_graphics();
	
	const graphics_t *const graphics = gs_get_context();
	const EGLDisplay edisp = graphics->device->plat->edisplay;
	ctx->edisp = edisp;
	
	// Find DRM video source
	ctx->lastGoodPlane = 0;
	ctx->dma_buf_fd = (int*)malloc(sizeof(int)*4);
	
	ctx->fb = prepareImage(drmfd);
	
	ctx->nplanes = 0;
	initDmaBufFDs(drmfd, ctx->fb, ctx->dma_buf_fd, &ctx->nplanes);
	
	blog(LOG_DEBUG, "Number of planes: %d", ctx->nplanes);
	if (ctx->nplanes == 0) {
		blog(LOG_ERROR, "Not permitted to get fb handles.");
		cleanupDmaBufFDs(ctx->fb, ctx->dma_buf_fd, &ctx->nplanes);
		close(drmfd);
		return 0;
	}

	blog(LOG_DEBUG, "KMSGrab: %dx%d %d", ctx->fb->width, ctx->fb->height, ctx->fb->pitches[0]);


	ctx->width = ctx->fb->width;
	ctx->height = ctx->fb->height;
	ctx->eimage = create_dmabuf_egl_image(ctx->edisp, ctx->fb->width, ctx->fb->height,
					    DRM_FORMAT_XRGB8888, ctx->nplanes, ctx->dma_buf_fd, 
					    ctx->fb->pitches, ctx->fb->offsets, ctx->fb->modifier);

	if (!ctx->eimage) {
		// FIXME stringify error
		blog(LOG_ERROR, "Cannot create EGLImage: %d", eglGetError());
		dmabuf_source_close(ctx);
		goto exit;
	}

	// FIXME handle fourcc?
	if (!ctx->texture)
		ctx->texture = gs_texture_create(ctx->fb->width, ctx->fb->height, GS_BGRA, 1, NULL, GS_DYNAMIC);
	const GLuint gltex = *(GLuint *)gs_texture_get_obj(ctx->texture);
	blog(LOG_DEBUG, "gltex = %x", gltex);
	glBindTexture(GL_TEXTURE_2D, gltex);

	glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, ctx->eimage);
	//glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	//glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

exit:
	obs_leave_graphics();
	return;
}

static void dmabuf_source_update(void *data, obs_data_t *settings)
{
	dmabuf_source_t *ctx = data;
	blog(LOG_DEBUG, "dmabuf_source_udpate %p", ctx);

	ctx->show_cursor = obs_data_get_bool(settings, "show_cursor");

	dmabuf_source_close(ctx);
	dmabuf_source_open(ctx);
}

static void *dmabuf_source_create(obs_data_t *settings, obs_source_t *source)
{
	blog(LOG_DEBUG, "dmabuf_source_create");
	(void)settings;

	{
		// FIXME is it desirable to enter graphics at create time?
		// Or is it better to postpone all activity to after the module
		// was succesfully and unconditionally created?
		obs_enter_graphics();
		const int device_type = gs_get_device_type();
		obs_leave_graphics();
		if (device_type != GS_DEVICE_OPENGL) {
			blog(LOG_ERROR, "dmabuf_source requires EGL");
			return NULL;
		}
	}

	// FIXME move to glad
	if (!glEGLImageTargetTexture2DOES) {
		glEGLImageTargetTexture2DOES =
			(PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)eglGetProcAddress(
				"glEGLImageTargetTexture2DOES");
	}

	if (!glEGLImageTargetTexture2DOES) {
		blog(LOG_ERROR, "GL_OES_EGL_image extension is required");
		return NULL;
	}

	dmabuf_source_t *ctx = bzalloc(sizeof(dmabuf_source_t));
	ctx->source = source;

	// cursor
	if (obs_get_nix_platform() == OBS_NIX_PLATFORM_X11_EGL) {
		ctx->xcb = xcb_connect(NULL, NULL);
		if (!ctx->xcb || xcb_connection_has_error(ctx->xcb)) {
			blog(LOG_ERROR,
			     "Unable to open X display, cursor will not be available");
		}

		ctx->cursor = xcb_xcursor_init(ctx->xcb);
	}
	else {
		ctx->show_cursor = false;
	}

	dmabuf_source_update(ctx, settings);
	return ctx;
}

static void dmabuf_source_close_fds(dmabuf_source_t *ctx)
{
	if (ctx->dma_buf_fd >= 0)
		close(ctx->dma_buf_fd);
}

static void dmabuf_source_destroy(void *data)
{
	dmabuf_source_t *ctx = data;
	blog(LOG_DEBUG, "dmabuf_source_destroy %p", ctx);

	if (ctx->texture)
		gs_texture_destroy(ctx->texture);
		
	if (ctx->drmfd)
		close(ctx->drmfd);

	dmabuf_source_close(ctx);
	dmabuf_source_close_fds(ctx);

	if (obs_get_nix_platform() == OBS_NIX_PLATFORM_X11_EGL) {
		if (ctx->cursor)
			xcb_xcursor_destroy(ctx->cursor);

		if (ctx->xcb)
			xcb_disconnect(ctx->xcb);
	}

	bfree(data);
}

static void dmabuf_source_render(void *data, gs_effect_t *effect)
{
	dmabuf_source_t *ctx = data;

	if (!ctx->texture)
		return;

	effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
	
	cleanupDmaBufFDs(ctx->fb, ctx->dma_buf_fd, &ctx->nplanes);
	
	ctx->fb = prepareImage(ctx->drmfd);
	if (ctx->width != ctx->fb->width || ctx->height != ctx->fb->height) {
		//ctx->lastGoodPlane = 0;
		gs_texture_destroy(ctx->texture);
		ctx->width = ctx->fb->width;
		ctx->height = ctx->fb->height;
		ctx->texture = gs_texture_create(ctx->fb->width, ctx->fb->height, GS_BGRA, 1, NULL, GS_DYNAMIC);
	}
	initDmaBufFDs(ctx->drmfd, ctx->fb, ctx->dma_buf_fd, &ctx->nplanes);
	
	eglDestroyImage(ctx->edisp, ctx->eimage);
	ctx->eimage = create_dmabuf_egl_image(ctx->edisp, ctx->fb->width, ctx->fb->height,
					    DRM_FORMAT_XRGB8888, ctx->nplanes, ctx->dma_buf_fd, 
					    ctx->fb->pitches, ctx->fb->offsets, ctx->fb->modifier);
	
	const GLuint gltex = *(GLuint *)gs_texture_get_obj(ctx->texture);
	glBindTexture(GL_TEXTURE_2D, gltex);
	
	glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, ctx->eimage);

	gs_draw_sprite(ctx->texture, 0, 0, 0);

	if (ctx->show_cursor && ctx->cursor) {
		xcb_xcursor_update(ctx->xcb, ctx->cursor);
		xcb_xcursor_render(ctx->cursor);
	}
}

static void dmabuf_source_get_defaults(obs_data_t *defaults)
{
	obs_data_set_default_bool(defaults, "show_cursor", true);
}

static obs_properties_t *dmabuf_source_get_properties(void *data)
{
	dmabuf_source_t *ctx = data;

	obs_properties_t *props = obs_properties_create();

	obs_properties_add_bool(props, "show_cursor",
				obs_module_text("CaptureCursor"));

	return props;
}

static const char *dmabuf_source_get_name(void *data)
{
	blog(LOG_DEBUG, "dmabuf_source_get_name %p", data);
	return "DMA-BUF source";
}

static uint32_t dmabuf_source_get_width(void *data)
{
	const dmabuf_source_t *ctx = data;
	return ctx->width;
}

static uint32_t dmabuf_source_get_height(void *data)
{
	const dmabuf_source_t *ctx = data;
	return ctx->height;
}

struct obs_source_info dmabuf_input = {
	.id = "dmabuf-source",
	.type = OBS_SOURCE_TYPE_INPUT,
	.get_name = dmabuf_source_get_name,
	.output_flags = OBS_SOURCE_VIDEO |
			OBS_SOURCE_DO_NOT_DUPLICATE,
	.create = dmabuf_source_create,
	.destroy = dmabuf_source_destroy,
	.video_render = dmabuf_source_render,
	.get_width = dmabuf_source_get_width,
	.get_height = dmabuf_source_get_height,
	.get_defaults = dmabuf_source_get_defaults,
	.get_properties = dmabuf_source_get_properties,
	.update = dmabuf_source_update,
};

OBS_DECLARE_MODULE()

OBS_MODULE_USE_DEFAULT_LOCALE("linux-dmabuf", "en-US")
MODULE_EXPORT const char *obs_module_description(void)
{
	return "libdrm/dma-buf based screen capture for linux";
}

bool obs_module_load(void)
{
	if (obs_get_nix_platform() != OBS_NIX_PLATFORM_X11_EGL && obs_get_nix_platform() != OBS_NIX_PLATFORM_WAYLAND) {
		blog(LOG_ERROR, "linux-dmabuf cannot run on non-EGL platforms");
		return false;
	}

	obs_register_source(&dmabuf_input);
	return true;
}

void obs_module_unload(void)
{
}
