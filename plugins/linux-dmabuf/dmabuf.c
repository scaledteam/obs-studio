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

/*typedef struct {
	drmsend_response_t resp;
	int fb_fds[OBS_DRMSEND_MAX_FRAMEBUFFERS];
} dmabuf_source_fblist_t;*/

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

	//dmabuf_source_fblist_t fbs;
	int active_fb;

	bool show_cursor;
	
	// my
	drmModePlanePtr plane;
	uint32_t lastGoodPlane;
	
	int dma_buf_fd;
	drmModeFBPtr fb;
	int drmfd;
	int width;
	int height;
} dmabuf_source_t;

// get planes directly
static uint32_t prepareImage(void *data, const int fd)
{
	dmabuf_source_t *context = data;
	
	drmModePlaneResPtr planes = drmModeGetPlaneResources(fd);
	
	// Check the first plane (or last good)
	drmModePlanePtr plane = drmModeGetPlane(fd, planes->planes[context->lastGoodPlane]);
	uint32_t fb_id = plane->fb_id;
	drmModeFreePlane(plane);
	
	// Find a good plane
	if (fb_id == 0) {
		for (uint32_t i = 0; i < planes->count_planes; ++i) {
			drmModePlanePtr plane = drmModeGetPlane(fd, planes->planes[i]);
			context->plane = plane;
			
			if (plane->fb_id != 0) {
				context->lastGoodPlane = i;
				fb_id = plane->fb_id;
				//MSG("%d, %#x", i, fb_id);
				
				drmModeFreePlane(plane);
				break;
			}
			else {
				drmModeFreePlane(plane);
			}
		}
	}
	
	drmModeFreePlaneResources(planes);
	
	//MSG("%#x", fb_id);
	//blog(LOG_ERROR, "prepare image fb_id is %#x", fb_id);
	return fb_id;
}


static void dmabuf_source_close(dmabuf_source_t *ctx)
{
	blog(LOG_DEBUG, "dmabuf_source_close %p", ctx);

	if (ctx->eimage != EGL_NO_IMAGE) {
		eglDestroyImage(ctx->edisp, ctx->eimage);
		ctx->eimage = EGL_NO_IMAGE;
	}

	ctx->active_fb = -1;
}

static void dmabuf_source_open(dmabuf_source_t *ctx, uint32_t fb_id)
{
	blog(LOG_DEBUG, "dmabuf_source_open %p %#x", ctx, fb_id);
	/*assert(ctx->active_fb == -1);

	int index;
	for (index = 0; index < ctx->fbs.resp.num_framebuffers; ++index)
		if (fb_id == ctx->fbs.resp.framebuffers[index].fb_id)
			break;

	if (index == ctx->fbs.resp.num_framebuffers) {
		blog(LOG_ERROR, "Framebuffer id=%#x not found", fb_id);
		return;
	}*/
	dmabuf_source_t *context = ctx;
	
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
		
	// Find DRM video source
	//uint32_t fb_id = prepareImage(ctx, drmfd);
	fb_id = prepareImage(ctx, drmfd);
	//blog(LOG_INFO, "Got fb_id=%#x", fb_id);

	if (fb_id == 0) {
		blog(LOG_DEBUG, "Not found fb_id");
		return;
	}

	context->dma_buf_fd = -1;
	drmModeFBPtr fb = drmModeGetFB(drmfd, fb_id);
	ctx->fb = fb;
	if (!fb->handle) {
		blog(LOG_DEBUG, "Not permitted to get fb handles.");
		
		if (context->dma_buf_fd >= 0)
			close(context->dma_buf_fd);
		if (ctx->fb)
			drmModeFreeFB(ctx->fb);
		close(drmfd);
		return;
	}

	blog(LOG_DEBUG, "Using framebuffer id=%#x (index=%d)", fb_id, index);

	//const drmsend_framebuffer_t *fb = ctx->fbs.resp.framebuffers + index;

	blog(LOG_DEBUG, "%dx%d %d", fb->width, fb->height, fb->pitch);

	obs_enter_graphics();
	
	drmPrimeHandleToFD(drmfd, fb->handle, 0, &context->dma_buf_fd);

	const graphics_t *const graphics = gs_get_context();
	const EGLDisplay edisp = graphics->device->plat->edisplay;
	ctx->edisp = edisp;

	/* clang-format off */
	// FIXME check for EGL_EXT_image_dma_buf_import
	EGLAttrib eimg_attrs[] = {
		EGL_WIDTH, fb->width,
		EGL_HEIGHT, fb->height,
		EGL_LINUX_DRM_FOURCC_EXT, DRM_FORMAT_XRGB8888,
		EGL_DMA_BUF_PLANE0_FD_EXT, context->dma_buf_fd,
		EGL_DMA_BUF_PLANE0_OFFSET_EXT, 0,
		EGL_DMA_BUF_PLANE0_PITCH_EXT, fb->pitch,
		EGL_NONE
	};
	/* clang-format on */

	ctx->width = fb->width;
	ctx->height = fb->height;
	ctx->eimage = eglCreateImage(edisp, EGL_NO_CONTEXT,
				     EGL_LINUX_DMA_BUF_EXT, 0, eimg_attrs);

	if (!ctx->eimage) {
		// FIXME stringify error
		blog(LOG_ERROR, "Cannot create EGLImage: %d", eglGetError());
		dmabuf_source_close(ctx);
		goto exit;
	}

	// FIXME handle fourcc?
	ctx->texture = gs_texture_create(fb->width, fb->height, GS_BGRA, 1,
					 NULL, GS_DYNAMIC);
	const GLuint gltex = *(GLuint *)gs_texture_get_obj(ctx->texture);
	blog(LOG_DEBUG, "gltex = %x", gltex);
	glBindTexture(GL_TEXTURE_2D, gltex);

	glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, ctx->eimage);
	//glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	//glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

	//ctx->active_fb = index;

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
	dmabuf_source_open(ctx, obs_data_get_int(settings, "framebuffer"));
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
	ctx->active_fb = -1;

#define COUNTOF(a) (sizeof(a) / sizeof(*a))
	/*for (int i = 0; i < (int)COUNTOF(ctx->fbs.fb_fds); ++i) {
		ctx->fbs.fb_fds[i] = -1;
	}

	if (!dmabuf_source_receive_framebuffers(&ctx->fbs)) {
		blog(LOG_ERROR, "Unable to enumerate DRM/KMS framebuffers");
		bfree(ctx);
		return NULL;
	}*/

	ctx->xcb = xcb_connect(NULL, NULL);
	if (!ctx->xcb || xcb_connection_has_error(ctx->xcb)) {
		blog(LOG_ERROR,
		     "Unable to open X display, cursor will not be available");
	}

	ctx->cursor = xcb_xcursor_init(ctx->xcb);

	dmabuf_source_update(ctx, settings);
	return ctx;
}

static void dmabuf_source_close_fds(dmabuf_source_t *ctx)
{
	/*for (int i = 0; i < ctx->fbs.resp.num_framebuffers; ++i) {
		const int fd = ctx->fbs.fb_fds[i];
		if (fd > 0)
			close(fd);
	}*/
	if (ctx->dma_buf_fd >= 0)
		close(ctx->dma_buf_fd);
	if (ctx->fb)
		drmModeFreeFB(ctx->fb);
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

	if (ctx->cursor)
		xcb_xcursor_destroy(ctx->cursor);

	if (ctx->xcb)
		xcb_disconnect(ctx->xcb);

	bfree(data);
}

static void dmabuf_source_video_tick(void *data, float seconds)
{
	UNUSED_PARAMETER(seconds);
	dmabuf_source_t *ctx = data;

	if (!ctx->texture)
		return;
	if (!obs_source_showing(ctx->source))
		return;
	/*if (!ctx->cursor)
		return;

	xcb_xfixes_get_cursor_image_cookie_t cur_c =
		xcb_xfixes_get_cursor_image_unchecked(ctx->xcb);
	xcb_xfixes_get_cursor_image_reply_t *cur_r =
		xcb_xfixes_get_cursor_image_reply(ctx->xcb, cur_c, NULL);

	obs_enter_graphics();
	xcb_xcursor_update(ctx->cursor, cur_r);
	obs_leave_graphics();

	free(cur_r);*/
}

static void dmabuf_source_render(void *data, gs_effect_t *effect)
{
	dmabuf_source_t *ctx = data;

	if (!ctx->texture)
		return;

	effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
	
	// Find DRM video source
	uint32_t fb_id = prepareImage(data, ctx->drmfd);

	if (fb_id == 0) {
		blog(LOG_ERROR, "Not found fb_id");
	}
	else {
		if (ctx->dma_buf_fd >= 0)
			close(ctx->dma_buf_fd);
		if (ctx->fb)
			drmModeFreeFB(ctx->fb);
			
		drmModeFBPtr fb = drmModeGetFB(ctx->drmfd, fb_id);
		if (!fb->handle) {
			blog(LOG_ERROR, "Not permitted to get fb handles");
			
			if (fb)
				drmModeFreeFB(fb);
			close(ctx->drmfd);
			return;
		}
		ctx->fb = fb;
		
		EGLAttrib eimg_attrs[] = {
			EGL_WIDTH, fb->width,
			EGL_HEIGHT, fb->height,
			EGL_LINUX_DRM_FOURCC_EXT, DRM_FORMAT_XRGB8888,
			EGL_DMA_BUF_PLANE0_FD_EXT, ctx->dma_buf_fd,
			EGL_DMA_BUF_PLANE0_OFFSET_EXT, 0,
			EGL_DMA_BUF_PLANE0_PITCH_EXT, fb->pitch,
			EGL_NONE
		};
		
		drmPrimeHandleToFD(ctx->drmfd, fb->handle, 0, &ctx->dma_buf_fd);
		eglDestroyImage(ctx->edisp, ctx->eimage);
		ctx->eimage = eglCreateImage(ctx->edisp, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, 0,
			eimg_attrs);
		
		//gs_texture_destroy(ctx->texture);
		//ctx->texture = gs_texture_create(fb->width, fb->height, GS_BGRA, 1, NULL, GS_DYNAMIC);
		const GLuint gltex = *(GLuint *)gs_texture_get_obj(ctx->texture);
		//blog(LOG_DEBUG, "gltex = %x", gltex);
		glBindTexture(GL_TEXTURE_2D, gltex);
		
		glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, ctx->eimage);
	}

	gs_eparam_t *image = gs_effect_get_param_by_name(effect, "image");
	gs_effect_set_texture(image, ctx->texture);

	while (gs_effect_loop(effect, "Draw")) {
		gs_draw_sprite(ctx->texture, 0, 0, 0);
	}

	if (ctx->show_cursor && ctx->cursor) {
		xcb_xcursor_update(ctx->xcb, ctx->cursor);
		while (gs_effect_loop(effect, "Draw")) {
			xcb_xcursor_render(ctx->cursor);
		}
	}
}

static void dmabuf_source_get_defaults(obs_data_t *defaults)
{
	obs_data_set_default_bool(defaults, "show_cursor", true);
}

static obs_properties_t *dmabuf_source_get_properties(void *data)
{
	dmabuf_source_t *ctx = data;
	/*blog(LOG_DEBUG, "dmabuf_source_get_properties %p", ctx);

	dmabuf_source_fblist_t stack_list = {0};

	if (!dmabuf_source_receive_framebuffers(&stack_list)) {
		blog(LOG_ERROR, "Unable to enumerate DRM/KMS framebuffers");
		return NULL;
	}*/

	obs_properties_t *props = obs_properties_create();

	obs_properties_add_bool(props, "show_cursor",
				obs_module_text("CaptureCursor"));

	/*obs_property_t *fb_list = obs_properties_add_list(
		props, "framebuffer", "Framebuffer to capture",
		OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);

	for (int i = 0; i < stack_list.resp.num_framebuffers; ++i) {
		const drmsend_framebuffer_t *fb =
			stack_list.resp.framebuffers + i;
		char buf[128];
		sprintf(buf, "%dx%d (%#x)", fb->width, fb->height, fb->fb_id);
		obs_property_list_add_int(fb_list, buf, fb->fb_id);
	}

	dmabuf_source_close_fds(ctx);
	memcpy(&ctx->fbs, &stack_list, sizeof(stack_list));*/

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
	.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW |
			OBS_SOURCE_DO_NOT_DUPLICATE,
	.create = dmabuf_source_create,
	.destroy = dmabuf_source_destroy,
	.video_tick = dmabuf_source_video_tick,
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
