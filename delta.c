/*
 * Simple layout generator for River, with swapable layouts
 *
 * Based on example from:
 * https://codeberg.org/river/river/src/branch/master/contrib/layout.c
 */
#include <assert.h>
#include <ctype.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <wayland-client-protocol.h>
#include <wayland-client.h>

#include "river-layout-v3.h"

/* A few macros to indulge the inner glibc user. */
#define MIN(a, b) (a < b ? a : b)
#define MAX(a, b) (a > b ? a : b)
#define CLAMP(a, b, c) (MIN(MAX(b, c), MAX(MIN(b, c), a)))

/* Define number of views */
#define LAYOUT_STYLE_COUNT 6

/* Create an enum to describe the layout state */
enum LayoutStyle {
  TILE,        // Normal Tiled Layout
  SPIRAL,      // Fibonacci Spiral
  DIMINISHING, // Dimminishing Spiral
  COLUMN,      // Equal sized columns
  STACK,       // Equal sized rows
  GRID,        // Equal sized rows and columns
};

struct Output {
  struct wl_list link;

  struct wl_output *output;
  struct river_layout_v3 *layout;

  uint32_t main_count;
  double main_ratio;
  uint32_t view_padding;
  uint32_t outer_padding;
  enum LayoutStyle layout_style;

  bool configured;
};

// Global variables for the parameters that can be passed in as arguments
uint32_t global_main_count = 1;
double global_main_ratio = 0.5;
uint32_t global_view_padding = 5;
uint32_t global_outer_padding = 5;

/* In Wayland it's a good idea to have your main data global, since you'll need
 * it everywhere anyway.
 */
struct wl_display *wl_display;
struct wl_registry *wl_registry;
struct wl_callback *sync_callback;
struct river_layout_manager_v3 *layout_manager;
struct wl_list outputs;
bool loop = true;
int ret = EXIT_FAILURE;

/**
 * Handle a layout request for a Tiled layout
 *
 * The tiled layout has a set of main windows (laid out in a stack),
 * and another column (also laid out in a stack)
 *
 * @param view_count number of views in the layout
 * @param width width of the usable area
 * @param height height of the usable area
 * @param tags tags of teh output, 32-bit bitfield
 * @param serial serial of the layout demand
 * */
static void delta_handle_layout_demand_tile(
    struct Output *output, struct river_layout_v3 *river_layout_v3,
    uint32_t view_count, uint32_t width, uint32_t height, uint32_t tags,
    uint32_t serial) {
  /* Simple tiled layout with no frills.*/

  // Start by calculating the width and the height after accounting for the
  // padding
  width -= 2 * output->outer_padding, height -= 2 * output->outer_padding;
  unsigned int main_size, // Size (width) of the main column
      stack_size,         // Size (width) of the stack
      view_x,             // x-coord OFFSET of the view (from the left)
      view_y,             // y-coord OFFSET of the view (from the top)
      view_width,         // Width of the view
      view_height;        // Height of the view
  // If the number of views to be put in the main column is 0, set
  // the main size to 0 and the stack size to the full width
  if (output->main_count == 0) {
    main_size = 0;
    stack_size = width;
  } else if (view_count <= output->main_count) {
    /* If all of the views are to be assigned to the main stack, set the
     * main size to be the full width, and the stack size to 0 */
    main_size = width;
    stack_size = 0;
  } else {
    /* Otherwise, set the main size to the the width multiplied by the
     * main ratio, and the stacksize to be the remainder of the usable area*/
    main_size = width * output->main_ratio;
    stack_size = width - main_size;
  }
  // Iterate through each view, starting from the top of the stack
  // NOTE: The view/inner padding is handled in the push view dimensions call
  // below
  for (unsigned int i = 0; i < view_count; i++) {
    if (i < output->main_count) {
      // The main area
      view_x = 0;             // The offset for the main area is 0
      view_width = main_size; // The width of the main area is the main_size
      view_height =
          height /
          MIN(output->main_count,
              view_count); // The height is divided equally among all main views
      view_y = i * view_height; // Offset is the number of views above this one
                                // multiplied by their height
    } else {
      // Stack area
      view_x =
          main_size; // This area starts after the full width of the main area
      view_width = stack_size; // The width is the previously calculated width
                               // of the stack size
      view_height =
          height /
          (view_count -
           output->main_count); // Height is divided equally among views
      view_y = (i - output->main_count) *
               view_height; // View offset calculated from number of views
                            // above, and their heights
    }

    // Submit the dimensions to the view
    river_layout_v3_push_view_dimensions(
        output->layout, // This is just passed in from the data
        view_x + output->view_padding +
            output
                ->outer_padding, // The x-coord is the offset from above, plus
                                 // the view padding. This is added to the
                                 // outer_padding to get the actual x-coordinate
        view_y + output->view_padding +
            output->outer_padding, // Same as the x-coord
        view_width -
            (2 * output->view_padding), // Width, accounting for desired padding
        view_height - (2 * output->view_padding),
        serial); // Height, accounting for desired padding
  }
  // Commit the layout (finalize the layout which was set for the various views)
  river_layout_v3_commit(output->layout, "[]=", serial);
}

static void delta_handle_layout_demand_spiral(
    struct Output *output, struct river_layout_v3 *river_layout_v3,
    uint32_t view_count, uint32_t width, uint32_t height, uint32_t tags,
    uint32_t serial, bool diminish) {
  width -= 2 * output->outer_padding, height -= 2 * output->outer_padding;
  printf("Usable width is: %d", width);
  printf("Usable height is: %d", height);
  unsigned int view_x, view_y, view_width, view_height;
  view_x = 0;
  view_y = 0;
  // View width starts as full width
  view_width = width;
  // Same with height
  view_height = height;
  for (unsigned int i = 0; i < view_count; i++) {
    if (i == view_count - 1) {
      // For the last view, just take the full width/height
      river_layout_v3_push_view_dimensions(
          output->layout, view_x + output->view_padding + output->outer_padding,
          view_y + output->view_padding + output->outer_padding,
          view_width - (2 * output->view_padding),
          view_height - (2 * output->view_padding), serial);
    } else {

      if (i % 2 == 0) {
        // If i is even, the width will be split
        view_width /= 2;
        if ((i % 4 == 2) && !diminish) {
          // View is on the right side
          river_layout_v3_push_view_dimensions(
              output->layout,
              view_x + output->view_padding + output->outer_padding +
                  view_width,
              view_y + output->view_padding + output->outer_padding,
              view_width - (2 * output->view_padding),
              view_height - (2 * output->view_padding), serial);
        } else {
          // View is on the left side
          river_layout_v3_push_view_dimensions(
              output->layout,
              view_x + output->view_padding + output->outer_padding,
              view_y + output->view_padding + output->outer_padding,
              view_width - (2 * output->view_padding),
              view_height - (2 * output->view_padding), serial);
          view_x += view_width;
        }
      } else {
        // If i is odd, the height will be split
        view_height /= 2;
        if ((i % 4 == 3) && !diminish) {
          // View is on the up side
          river_layout_v3_push_view_dimensions(
              output->layout,
              view_x + output->view_padding + output->outer_padding,
              view_y + output->view_padding + output->outer_padding +
                  view_height,
              view_width - (2 * output->view_padding),
              view_height - (2 * output->view_padding), serial);
        } else {
          // View is on the down side
          river_layout_v3_push_view_dimensions(
              output->layout,
              view_x + output->view_padding + output->outer_padding,
              view_y + output->view_padding + output->outer_padding,
              view_width - (2 * output->view_padding),
              view_height - (2 * output->view_padding), serial);
          view_y += view_height;
        }
      }
    }
  }
  if (diminish) {
    river_layout_v3_commit(output->layout, "↘", serial);
  } else {
    river_layout_v3_commit(output->layout, "@", serial);
  }
}

/**
 * Handle a layout request for a column layout
 *
 * This layout is just equal sized columns for each view
 *
 * @param view_count number of views in the layout
 * @param width width of the usable area
 * @param height height of the usable area
 * @param tags tags of teh output, 32-bit bitfield
 * @param serial serial of the layout demand
 * */
static void delta_handle_layout_demand_column(
    struct Output *output, struct river_layout_v3 *river_layout_v3,
    uint32_t view_count, uint32_t width, uint32_t height, uint32_t tags,
    uint32_t serial) {
  // Find the usable width and height accounting for padding
  width -= 2 * output->outer_padding, height -= 2 * output->outer_padding;
  unsigned int view_x,  // x-coord offset
      view_outer_width, // Total width of view (including padding)
      view_inner_width; // width of view (without padding)
  view_outer_width = width / view_count;
  view_inner_width = view_outer_width - (2 * output->view_padding);
  for (unsigned int i = 0; i < view_count; i++) {
    view_x = i * view_outer_width;
    river_layout_v3_push_view_dimensions(
        output->layout,
        view_x + output->view_padding + output->outer_padding, // x-coord
        output->outer_padding,                                 // y-coord
        view_inner_width, // Width of view (after padding accounted for)
        height,           // Full usable height
        serial);
  }
  river_layout_v3_commit(output->layout, "|||", serial);
}

/**
 * Handle a layout request for a stacked layout
 *
 * This layout is just a single stack across the entire usable width
 *
 * @param view_count number of views in the layout
 * @param width width of the usable area
 * @param height height of the usable area
 * @param tags tags of teh output, 32-bit bitfield
 * @param serial serial of the layout demand
 * */
static void delta_handle_layout_demand_stack(
    struct Output *output, struct river_layout_v3 *river_layout_v3,
    uint32_t view_count, uint32_t width, uint32_t height, uint32_t tags,
    uint32_t serial) {
  // Start by calculating the available width and height after accocunting
  // for the outer padding
  width -= 2 * output->outer_padding, height -= 2 * output->outer_padding;
  unsigned int view_y,   // y-coord offset
      view_outer_height, // Total height of view (including padding)
      view_inner_height; // Height of view (without padding)
  view_outer_height = height / view_count;
  view_inner_height = view_outer_height - (2 * output->view_padding);
  // Iterate through all of the views, starting from the top of the stack
  for (unsigned int i = 0; i < view_count; i++) {
    view_y = i * view_outer_height;
    river_layout_v3_push_view_dimensions(
        output->layout,
        output->outer_padding, // View x-coord is just the outer_padding
        view_y + output->view_padding +
            output->outer_padding, // View y-coord accounting for padding
        width,                     // Just full usable width
        view_inner_height, // Height of the view (accounting for padding )
        serial);
  }
  river_layout_v3_commit(output->layout, "=", serial);
}

/**
 * Handle a layout request for a grid layout
 *
 * This layout is a grid of views, with an equal number of rows and columns
 *
 * @param view_count number of views in the layout
 * @param width width of the usable area
 * @param height height of the usable area
 * @param tags tags of teh output, 32-bit bitfield
 * @param serial serial of the layout demand
 * */
static void delta_handle_layout_demand_grid(
    struct Output *output, struct river_layout_v3 *river_layout_v3,
    uint32_t view_count, uint32_t width, uint32_t height, uint32_t tags,
    uint32_t serial) {
  // Start by calculating the available width and height after accocunting
  // for the outer padding
  width -= 2 * output->outer_padding, height -= 2 * output->outer_padding;
  // Also calculate the number of rows/cols
  uint32_t grid_size = floor(sqrt(view_count));
  if (grid_size * grid_size < view_count) {
    grid_size++;
  }
  unsigned int view_x,   // x-coord offset
      view_y,            // y-coord offset
      view_outer_height, // height of view (including view padding)
      view_inner_height, // Height of view (without padding)
      view_outer_width,  // width of view (including view padding)
      view_inner_width,  // width of view (without padding)
      row,               // Row of the view
      col;               // Column of the view
  view_outer_height = height / grid_size; // Equally divide the height into rows
  view_outer_width = width / grid_size; // Equally divide the width into columns
  view_inner_height = view_outer_height - (2 * output->view_padding);
  view_inner_width = view_outer_width - (2 * output->view_padding);
  // Iterate through all of the views, starting from the top of the stack
  // In row major order
  for (unsigned int i = 0; i < view_count; i++) {
    // Find the row
    row = i / grid_size;
    col = i % grid_size;
    view_x = col * view_outer_width;
    view_y = row * view_outer_height;
    river_layout_v3_push_view_dimensions(
        output->layout,
        view_x + output->view_padding + output->outer_padding, // View x-coord
        view_y + output->view_padding + output->outer_padding, // View y-coord
        view_inner_width,  // Just full usable width
        view_inner_height, // Height of the view (accounting for padding )
        serial);
  }
  river_layout_v3_commit(output->layout, "#", serial);
}

static void delta_handle_layout_demand(void *data,
                                       struct river_layout_v3 *river_layout_v3,
                                       uint32_t view_count, uint32_t width,
                                       uint32_t height, uint32_t tags,
                                       uint32_t serial) {
  struct Output *output = (struct Output *)data;
  switch (output->layout_style) {
  case TILE:
    delta_handle_layout_demand_tile(output, river_layout_v3, view_count, width,
                                    height, tags, serial);
    break;
  case SPIRAL:
    delta_handle_layout_demand_spiral(output, river_layout_v3, view_count,
                                      width, height, tags, serial, false);
    break;
  case DIMINISHING:
    delta_handle_layout_demand_spiral(output, river_layout_v3, view_count,
                                      width, height, tags, serial, true);
    break;
  case COLUMN:
    delta_handle_layout_demand_column(output, river_layout_v3, view_count,
                                      width, height, tags, serial);
    break;
  case STACK:
    delta_handle_layout_demand_stack(output, river_layout_v3, view_count, width,
                                     height, tags, serial);
    break;
  case GRID:
    delta_handle_layout_demand_grid(output, river_layout_v3, view_count, width,
                                    height, tags, serial);
    break;
  }
}

static void
layout_handle_namespace_in_use(void *data,
                               struct river_layout_v3 *river_layout_v3) {
  /* Oh no, the namespace we choose is already used by another client!
   * All we can do now is destroy the river_layout object. Because we are
   * lazy, we just abort and let our cleanup mechanism destroy it. A more
   * sophisticated client could instead destroy only the one single
   * affected river_layout object and recover from this mishap. Writing
   * such a client is left as an exercise for the reader.
   */
  fputs("Namespace already in use.\n", stderr);
  loop = false;
}

static bool skip_whitespace(char **ptr) {
  if (*ptr == NULL)
    return false;
  while (isspace(**ptr)) {
    (*ptr)++;
    if (**ptr == '\0')
      return false;
  }
  return true;
}

static bool skip_nonwhitespace(char **ptr) {
  if (*ptr == NULL)
    return false;
  while (!isspace(**ptr)) {
    (*ptr)++;
    if (**ptr == '\0')
      return false;
  }
  return true;
}

static const char *get_second_word(char **ptr, const char *name) {
  /* Skip to the next word. */
  if (!skip_nonwhitespace(ptr) || !skip_whitespace(ptr)) {
    fprintf(stderr, "ERROR: Too few arguments. '%s' needs one argument.\n",
            name);
    return NULL;
  }

  /* Now we know where the second word begins. */
  const char *second_word = *ptr;

  /* Check if there is a third word. */
  if (skip_nonwhitespace(ptr) && skip_whitespace(ptr)) {
    fprintf(stderr, "ERROR: Too many arguments. '%s' needs one argument.\n",
            name);
    return NULL;
  }

  return second_word;
}

static void handle_uint32_command(char **ptr, uint32_t *value,
                                  const char *name) {
  const char *second_word = get_second_word(ptr, name);
  if (second_word == NULL)
    return;
  const int32_t arg = atoi(second_word);
  if (*second_word == '+' || *second_word == '-')
    *value = (uint32_t)MAX((int32_t)*value + arg, 0);
  else
    *value = (uint32_t)MAX(arg, 0);
}

static void handle_float_command(char **ptr, double *value, const char *name,
                                 double clamp_upper, double clamp_lower) {
  const char *second_word = get_second_word(ptr, name);
  if (second_word == NULL)
    return;
  const double arg = atof(second_word);
  if (*second_word == '+' || *second_word == '-')
    *value = CLAMP(*value + arg, clamp_upper, clamp_lower);
  else
    *value = CLAMP(arg, clamp_upper, clamp_lower);
}

static bool word_comp(const char *word, const char *comp) {
  if (strncmp(word, comp, strlen(comp)) == 0) {
    const char *after_comp = word + strlen(comp);
    if (isspace(*after_comp) || *after_comp == '\0')
      return true;
  }
  return false;
}

static void
layout_handle_user_command(void *data,
                           struct river_layout_v3 *river_layout_manager_v3,
                           const char *_command) {
  /* The user_command event will be received whenever the user decided to
   * send us a command. As an example, commands can be used to change the
   * layout values. Parsing the commands is the job of the layout
   * generator, the server just sends us the raw string.
   *
   * After this event is recevied, the views on the output will be
   * re-arranged and so we will also receive a layout_demand event.
   */

  struct Output *output = (struct Output *)data;

  /* Skip preceding whitespace. */
  char *command = (char *)_command;
  if (!skip_whitespace(&command))
    return;

  if (word_comp(command, "main_count"))
    handle_uint32_command(&command, &output->main_count, "main_count");
  else if (word_comp(command, "view_padding"))
    handle_uint32_command(&command, &output->view_padding, "view_padding");
  else if (word_comp(command, "outer_padding"))
    handle_uint32_command(&command, &output->outer_padding, "outer_padding");
  else if (word_comp(command, "main_ratio"))
    handle_float_command(&command, &output->main_ratio, "main_ratio", 0.1, 0.9);
  else if (word_comp(command, "reset")) {
    /* This is an example of a command that does something different
     * than just modifying a value. It resets all values to their
     * defaults.
     */

    if (skip_nonwhitespace(&command) && skip_whitespace(&command)) {
      fputs("ERROR: Too many arguments. 'reset' has no arguments.\n", stderr);
      return;
    }

    output->main_count = global_main_count;
    output->main_ratio = global_main_ratio;
    output->view_padding = global_view_padding;
    output->outer_padding = global_outer_padding;
  } else if (word_comp(command, "swap_layout")) {
    // Check that no additional argument was passed
    if (skip_nonwhitespace(&command) && skip_whitespace(&command)) {
      fputs("ERROR: Too many arguments. 'swap' has no arguments.\n", stderr);
      return;
    }

    // Swap to next layout style
    output->layout_style = (output->layout_style + 1) % LAYOUT_STYLE_COUNT;

  } else
    fprintf(stderr, "ERROR: Unknown command: %s\n", command);
}

static const struct river_layout_v3_listener layout_listener = {
    .namespace_in_use = layout_handle_namespace_in_use,
    .layout_demand = delta_handle_layout_demand,
    .user_command = layout_handle_user_command,
};

static void configure_output(struct Output *output) {
  output->configured = true;

  /* The namespace of the layout is how the compositor chooses what layout
   * to use. It can be any arbitrary string. It should describe roughly
   * what kind of layout your client will create, so here we use "swapable".
   */
  output->layout = river_layout_manager_v3_get_layout(
      layout_manager, output->output, "swapable");
  river_layout_v3_add_listener(output->layout, &layout_listener, output);
}

static bool create_output(struct wl_output *wl_output) {
  struct Output *output = calloc(1, sizeof(struct Output));
  if (output == NULL) {
    fputs("Failed to allocate.\n", stderr);
    return false;
  }

  output->output = wl_output;
  output->layout = NULL;
  output->configured = false;

  /* These are the parameters of our layout. In this case, they are the
   * ones you'd typically expect from a dynamic tiling layout, but if you
   * are creative, you can do more. You can use any arbitrary amount of
   * all kinds of values in your layout. If the user wants to change a
   * value, the server lets us know using user_command event of the
   * river_layout object.
   *
   * A layout generator is responsible for having sane defaults for all
   * layout values. The server only sends user_command events when there
   * actually is a command the user wants to send us.
   */
  output->main_count = global_main_count;
  output->main_ratio = global_main_ratio;
  output->view_padding = global_view_padding;
  output->outer_padding = global_outer_padding;

  /* If we already have the river_layout_manager, we can get a
   * river_layout object for this output.
   */
  if (layout_manager != NULL)
    configure_output(output);

  wl_list_insert(&outputs, &output->link);
  return true;
}

static void destroy_output(struct Output *output) {
  if (output->layout != NULL)
    river_layout_v3_destroy(output->layout);
  wl_output_destroy(output->output);
  wl_list_remove(&output->link);
  free(output);
}

static void destroy_all_outputs() {
  struct Output *output, *tmp;
  wl_list_for_each_safe(output, tmp, &outputs, link) destroy_output(output);
}

static void registry_handle_global(void *data, struct wl_registry *registry,
                                   uint32_t name, const char *interface,
                                   uint32_t version) {
  if (strcmp(interface, river_layout_manager_v3_interface.name) == 0)
    layout_manager =
        wl_registry_bind(registry, name, &river_layout_manager_v3_interface, 1);
  else if (strcmp(interface, wl_output_interface.name) == 0) {
    struct wl_output *wl_output =
        wl_registry_bind(registry, name, &wl_output_interface, version);
    if (!create_output(wl_output)) {
      loop = false;
      ret = EXIT_FAILURE;
    }
  }
}

/* A no-op function we plug into listeners when we don't want to handle an
 * event. */
static void noop(void *data, struct wl_registry *registry, uint32_t name) {}

static const struct wl_registry_listener registry_listener = {
    .global = registry_handle_global, .global_remove = noop};

static void sync_handle_done(void *data, struct wl_callback *wl_callback,
                             uint32_t irrelevant) {
  wl_callback_destroy(wl_callback);
  sync_callback = NULL;

  /* When this function is called, the registry finished advertising all
   * available globals. Let's check if we have everything we need.
   */
  if (layout_manager == NULL) {
    fputs("Wayland compositor does not support river-layout-v3.\n", stderr);
    ret = EXIT_FAILURE;
    loop = false;
    return;
  }

  /* If outputs were registered before the river_layout_manager is
   * available, they won't have a river_layout, so we need to create those
   * here.
   */
  struct Output *output;
  wl_list_for_each(output, &outputs, link) if (!output->configured)
      configure_output(output);
}

static const struct wl_callback_listener sync_callback_listener = {
    .done = sync_handle_done,
};

static bool init_wayland(void) {
  /* We query the display name here instead of letting wl_display_connect()
   * figure it out itself, because libwayland (for legacy reasons) falls
   * back to using "wayland-0" when $WAYLAND_DISPLAY is not set, which is
   * generally not desirable.
   */
  const char *display_name = getenv("WAYLAND_DISPLAY");
  if (display_name == NULL) {
    fputs("WAYLAND_DISPLAY is not set.\n", stderr);
    return false;
  }

  wl_display = wl_display_connect(display_name);
  if (wl_display == NULL) {
    fputs("Can not connect to Wayland server.\n", stderr);
    return false;
  }

  wl_list_init(&outputs);

  /* The registry is a global object which is used to advertise all
   * available global objects.
   */
  wl_registry = wl_display_get_registry(wl_display);
  wl_registry_add_listener(wl_registry, &registry_listener, NULL);

  /* The sync callback we attach here will be called when all previous
   * requests have been handled by the server. This allows us to know the
   * end of the startup, at which point all necessary globals should be
   * bound.
   */
  sync_callback = wl_display_sync(wl_display);
  wl_callback_add_listener(sync_callback, &sync_callback_listener, NULL);

  return true;
}

static void finish_wayland(void) {
  if (wl_display == NULL)
    return;

  destroy_all_outputs();

  if (sync_callback != NULL)
    wl_callback_destroy(sync_callback);
  if (layout_manager != NULL)
    river_layout_manager_v3_destroy(layout_manager);

  wl_registry_destroy(wl_registry);
  wl_display_disconnect(wl_display);
}

int main(int argc, char *argv[]) {
  // Step through the arguments
  int arg_pointer = 1;
  while (arg_pointer < argc) {
    if (arg_pointer == argc - 1) {
      fputs("ERROR: Argument with no value. All arguments must have values.\n",
            stderr);
      break;
    }
    if (word_comp(argv[arg_pointer], "-main-count")) {
      global_main_count = MAX(atoi(argv[arg_pointer + 1]), 0);
    } else if (word_comp(argv[arg_pointer], "-main-ratio")) {
      global_main_ratio = CLAMP(atof(argv[arg_pointer + 1]), 0.0, 1.0);
    } else if (word_comp(argv[arg_pointer], "-view-padding")) {
      global_view_padding = MAX(atoi(argv[arg_pointer + 1]), 0);
    } else if (word_comp(argv[arg_pointer], "-outer-padding")) {
      global_outer_padding = MAX(atoi(argv[arg_pointer + 1]), 0);
    }
    arg_pointer += 2;
  }
  // printf("main count: %u", global_main_count);
  // printf("main ratio: %lf", global_main_ratio);
  // printf("view padding: %u", global_view_padding);
  // printf("outer padding: %u", global_outer_padding);

  if (init_wayland()) {
    ret = EXIT_SUCCESS;
    while (loop && wl_display_dispatch(wl_display) != -1)
      ;
  }
  finish_wayland();
  return ret;
}
