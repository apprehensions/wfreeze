#include <err.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#include <wayland-client.h>
#ifdef __linux__
#include <linux/memfd.h>
#endif

#include "wlr-layer-shell-unstable-v1-protocol.h"
#include "wlr-screencopy-unstable-v1-protocol.h"

typedef struct {
	uint32_t wl_name;
	struct wl_output *wl_output;
	struct zwlr_screencopy_frame_v1 *frame;
	struct wl_surface *surface;
	struct zwlr_layer_surface_v1 *layer_surface;
	struct wl_buffer *buffer;
	uint32_t flags;
	int32_t transform;
	bool attached, frozen;

	struct wl_list link;
} Output;

static struct wl_display *display;
static struct wl_registry *registry;
static struct wl_compositor *compositor;
static struct wl_shm *shm;
static struct zwlr_layer_shell_v1 *layer_shell;
static struct zwlr_screencopy_manager_v1 *screencopy_manager;

static struct wl_list outputs;

static void
noop()
{
	/*
	 * :3c
	 */
}

static struct wl_buffer *
buffershm_create(struct wl_shm *shm, enum wl_shm_format format,
		int32_t width, int32_t height, int32_t stride)
{
	int fd;
	struct wl_shm_pool *shm_pool;
	struct wl_buffer *buffer;
	int32_t size = stride * height;

#if defined(__linux__) || \
	((defined(__FreeBSD__) && (__FreeBSD_version >= 1300048)))
	fd = memfd_create("output-shm-buffer-pool",
		MFD_CLOEXEC | MFD_ALLOW_SEALING |
#if defined(MFD_NOEXEC_SEAL)
		MFD_NOEXEC_SEAL
#else
		0
#endif
	);
#else
	char template[] = "/tmp/outputbuf-XXXXXX";
#if defined(__OpenBSD__)
	fd = shm_mkstemp(template);
#else
	fd = mkostemp(template, O_CLOEXEC);
#endif
	if (fd < 0)
		return NULL;
#if defined(__OpenBSD__)
    shm_unlink(template);
#else
	unlink(template);
#endif
#endif

	if ((ftruncate(fd, size)) < 0) {
		close(fd);
		return NULL;
	}

#if defined(__linux__) || \
	((defined(__FreeBSD__) && (__FreeBSD_version >= 1300048)))
	fcntl(fd, F_ADD_SEALS, F_SEAL_GROW | F_SEAL_SHRINK | F_SEAL_SEAL);
#endif

	shm_pool = wl_shm_create_pool(shm, fd, size);
	buffer = wl_shm_pool_create_buffer(shm_pool, 0,
		width, height, stride, format);
	wl_shm_pool_destroy(shm_pool);
	close(fd);

	return buffer;
}

static void
layer_surface_configure(void *data, struct zwlr_layer_surface_v1 *surface,
                        uint32_t serial, uint32_t w, uint32_t h)
{
	Output *output = data;

	if (output->frame)
		return;

	zwlr_layer_surface_v1_ack_configure(surface, serial);

	if (output->attached) {
		wl_surface_commit(output->surface);
		return;
	}

	if (output->flags & ZWLR_SCREENCOPY_FRAME_V1_FLAGS_Y_INVERT) {
		wl_surface_set_buffer_transform(output->surface,
			WL_OUTPUT_TRANSFORM_FLIPPED_180);
	}

	wl_surface_attach(output->surface, output->buffer, 0, 0);
	wl_surface_commit(output->surface);
	output->attached = true;
}

static const struct zwlr_layer_surface_v1_listener layer_surface_listener = {
    .configure = layer_surface_configure,
    .closed = noop,
};

static void
screencopy_frame_handle_buffer(void *data,
		struct zwlr_screencopy_frame_v1 *frame,
		uint32_t format, uint32_t width, uint32_t height, uint32_t stride)
{
	Output *output = data;

	if (output->buffer)
		wl_buffer_destroy(output->buffer);
	if (!(output->buffer = buffershm_create(shm, format, width, height, stride)))
		err(EXIT_FAILURE, "buffershm_create");
	zwlr_screencopy_frame_v1_copy(output->frame, output->buffer);
}

static void
screencopy_frame_handle_flags(void *data,
	struct zwlr_screencopy_frame_v1 *frame, uint32_t flags)
{
	Output *output = data;
	output->flags = flags;
}

static void
screencopy_frame_handle_ready(void *data,
		struct zwlr_screencopy_frame_v1 *frame, uint32_t tv_sec_hi,
		uint32_t tv_sec_lo, uint32_t tv_nsec)
{
	Output *output = data;

	zwlr_screencopy_frame_v1_destroy(output->frame);
	output->frame = NULL;

	wl_surface_commit(output->surface);
}

static void
screencopy_frame_handle_failed(void *data,
		struct zwlr_screencopy_frame_v1 *frame)
{
	Output *output = data;

	errx(EXIT_FAILURE, "failed to frame output %d", output->wl_name);
}

static const struct zwlr_screencopy_frame_v1_listener screencopy_frame_listener = {
	.buffer = screencopy_frame_handle_buffer,
	.flags = screencopy_frame_handle_flags,
	.ready = screencopy_frame_handle_ready,
	.failed = screencopy_frame_handle_failed,
};

static void
surface_handle_enter(void *data, struct wl_surface *surface,
		struct wl_output *wl_output)
{
	Output *output = data;
	output->frozen = true;
}

static const struct wl_surface_listener surface_listener = {
	.enter = surface_handle_enter,
	.leave = noop
};

static void
output_destroy(Output *output)
{
	wl_list_remove(&output->link);
	wl_buffer_destroy(output->buffer);
	zwlr_layer_surface_v1_destroy(output->layer_surface);
	wl_surface_destroy(output->surface);
	wl_output_destroy(output->wl_output);
	free(output);
}

static void
outputs_destroy(void)
{
	Output *output, *tmp;

	wl_list_for_each_safe(output, tmp, &outputs, link)
		output_destroy(output);
}

static void
output_handle_geometry(void *data, struct wl_output *wl_output,
		int32_t x, int32_t y, int32_t w, int32_t h,
		int32_t subpixel, const char *make, const char *model, int32_t transform)
{
	Output *output = data;
	output->transform = transform;
}

static void 
output_handle_done(void *data, struct wl_output *wl_output)
{
	Output *output = data;

	output->frame = zwlr_screencopy_manager_v1_capture_output(
		screencopy_manager, 0, output->wl_output);
	zwlr_screencopy_frame_v1_add_listener(output->frame,
		&screencopy_frame_listener, output);

	output->surface = wl_compositor_create_surface(compositor);
	wl_surface_set_buffer_transform(output->surface, output->transform);
	wl_surface_add_listener(output->surface, &surface_listener, output);

	output->layer_surface = zwlr_layer_shell_v1_get_layer_surface(
		layer_shell, output->surface, output->wl_output,
		ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "freeze");
	zwlr_layer_surface_v1_set_exclusive_zone(output->layer_surface, -1);
	zwlr_layer_surface_v1_set_anchor(output->layer_surface,
		ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT |
		ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT);
	zwlr_layer_surface_v1_add_listener(output->layer_surface,
		&layer_surface_listener, output);

}

static const struct wl_output_listener output_listener = {
	.geometry = output_handle_geometry,
	.mode = noop,
	.done = output_handle_done,
	.scale = noop,
	.name = noop,
	.description = noop,
};

static void
registry_global(void *data, struct wl_registry *registry,
		uint32_t name, const char *interface, uint32_t version)
{
	if (!strcmp(interface, wl_compositor_interface.name))
		compositor = wl_registry_bind(registry, name, &wl_compositor_interface, 4);
	else if (!strcmp(interface, wl_shm_interface.name))
		shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
	else if (!strcmp(interface, zwlr_layer_shell_v1_interface.name))
		layer_shell = wl_registry_bind(registry, name,
			&zwlr_layer_shell_v1_interface, 2);
	else if (!strcmp(interface, zwlr_screencopy_manager_v1_interface.name))
		screencopy_manager = wl_registry_bind(registry, name,
			&zwlr_screencopy_manager_v1_interface, 1);
	else if (!strcmp(interface, wl_output_interface.name)) {
		Output *output = calloc(1, sizeof(Output));
		output->wl_output = wl_registry_bind(registry, name, &wl_output_interface, 4);
		output->wl_name = name;
		wl_list_insert(&outputs, &output->link);
		wl_output_add_listener(output->wl_output, &output_listener, output);
	}
}

static const struct wl_registry_listener registry_listener = {
	.global = registry_global,
	.global_remove = noop,
};

static void
setup(void)
{
	Output *output;

	if (!(display = wl_display_connect(NULL)))
		errx(EXIT_FAILURE, "failed to connect to wayland");

	wl_list_init(&outputs);

	registry = wl_display_get_registry(display);
	wl_registry_add_listener(registry, &registry_listener, NULL);
	wl_display_roundtrip(display);

	if (!compositor || !shm || !layer_shell || !screencopy_manager)
		errx(EXIT_FAILURE, "unsupported compositor");

	if (wl_list_empty(&outputs))
		errx(EXIT_FAILURE, "no outputs");

	wl_list_for_each(output, &outputs, link)
		while (!output->frozen)
			if (wl_display_dispatch(display) < 0)
				err(EXIT_FAILURE, "wl_display_dispatch");
}

static void
cleanup(void)
{
	outputs_destroy();
	zwlr_screencopy_manager_v1_destroy(screencopy_manager);
	zwlr_layer_shell_v1_destroy(layer_shell);
	wl_shm_destroy(shm);
	wl_compositor_destroy(compositor);
	wl_registry_destroy(registry);
	wl_display_disconnect(display);
}

int
main(int argc, char *argv[])
{
	pid_t pid;
	int status;

	if (argc < 2) {
		fprintf(stderr, "usage: %s cmd [arg ...]\n", argv[0]);
		return EXIT_FAILURE;
	}
	argc--;
	argv++;

	setup();

	switch ((pid = fork())) {
	case -1:
		err(EXIT_FAILURE, "fork");
	case 0:
		execvp(argv[0], argv);
		err(EXIT_FAILURE, "execvp");
	}
	if (waitpid(pid, &status, 0) != pid)
		err(EXIT_FAILURE, "waitpid");

	cleanup();

	return (WIFEXITED(status)) ? WEXITSTATUS(status) : EXIT_FAILURE;
}
