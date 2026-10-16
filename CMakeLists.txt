cmake_minimum_required(VERSION 3.20)
project(bumpkins LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
add_compile_options(-Wall -Wextra -Werror)
option(BUMPKINS_NATIVE "Build the native i386 Linux C runtime" ON)
option(BUMPKINS_PORTABLE_CORE "Build the portable 64-bit game core" OFF)
option(BUMPKINS_PORTABLE_GAME "Build the native 64-bit launcher" OFF)
if(BUMPKINS_PORTABLE_GAME)
  set(BUMPKINS_PORTABLE_CORE ON)
endif()

if(BUMPKINS_PORTABLE_CORE)
  if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "The portable core needs the 64-bit host compiler")
  endif()
  find_package(Threads REQUIRED)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
  set_source_files_properties(src/native/draw.c PROPERTIES
    COMPILE_OPTIONS "-Wno-unused-parameter")
  add_library(bumpkins_core STATIC src/native/fatal.c src/native/memory.c src/native/file.c src/native/settings.c src/native/profile.c src/native/clock.c src/native/sync.c src/native/timer.c src/native/kernel_handle.c
    src/native/path.c src/native/codepage.c src/native/unicode.c src/native/timezone.c
    src/native/module_names.c src/native/module.c src/native/modules.c src/native/module_services.c src/native/system.c src/native/process.c src/native/environment.c src/native/locale.c src/native/resources.c
    src/native/platform_models.c src/native/format.c src/native/game.c src/native/game_data.c src/native/game_assets.c src/native/mixer.c src/native/mixer_api.c
    src/native/draw.c src/native/cd.c src/native/sound.c src/native/audio.c src/native/window.c src/bridge/address.c src/bridge/heap.c src/bridge/random.c
    src/bridge/save_codec.c src/bridge/camera.c src/bridge/placement.c
    src/bridge/archive.c src/bridge/archive_write.c src/bridge/archive_models.c
    src/bridge/crt_primitives.c src/bridge/crt_models.c src/bridge/crt_numeric.c
    src/bridge/crt_format.c src/bridge/crt_math_base.c src/bridge/crt_numeric_startup.c src/bridge/numeric_state.c
    src/bridge/crt_number_models.c src/bridge/crt_math_models.c
    src/bridge/crt_math_errors.c src/bridge/crt_math_helpers.c src/bridge/crt_calculation.c
    src/bridge/crt_output.c src/bridge/crt_stream.c src/bridge/crt_descriptor.c
    src/bridge/crt_character.c src/bridge/crt_stdio.c src/bridge/crt_io_models.c
    src/bridge/crt_locale.c src/bridge/crt_locale_models.c src/bridge/crt_startup_data.c
    src/bridge/crt_errors.c src/bridge/crt_signals.c src/bridge/crt_bootstrap.c
    src/bridge/crt_runtime_models.c src/bridge/crt_time.c src/bridge/crt_time_models.c
    src/bridge/crt_exception_models.c src/bridge/crt_exception_runtime.c
    src/bridge/crt_cpp_objects.c src/bridge/crt_cpp_dispatch.c src/bridge/crt_seh.c
    src/bridge/allocation_unwind.c
    src/bridge/global_lifecycle.c src/bridge/static_initializers.c src/bridge/lifecycle_models.c
    src/bridge/object_initializers.c
    src/bridge/crt_heap.c src/bridge/crt_debug_heap.c src/bridge/crt_alloc.c
    src/bridge/allocation_models.c
    src/bridge/bootstrap_resources.c src/bridge/bootstrap_models.c
    src/bridge/startup.c src/bridge/input.c src/bridge/startup_models.c
    src/bridge/media_devices.c src/bridge/media_models.c
    src/bridge/input_support.c src/bridge/input_models.c
    src/bridge/loading_progress.c src/bridge/indexed_text.c
    src/bridge/session_controls.c src/bridge/session_models.c src/bridge/audio_control_models.c
    src/bridge/interface_audio.c src/bridge/cd_control.c
    src/bridge/sample_audio.c src/bridge/sample_effects.c src/bridge/audio_cues.c
    src/bridge/audio_routing.c src/bridge/resource_audio.c
    src/bridge/game_audio_models.c
    src/bridge/entity_lifecycle.c src/bridge/entity_lifecycle_models.c
    src/bridge/crt_input.c
    src/bridge/crt_entry.c
    src/bridge/world.c src/bridge/world_models.c
    src/bridge/dialog.c src/bridge/dialog_models.c
    src/bridge/coordinator.c src/bridge/mission_io.c src/bridge/letters.c
    src/bridge/script_records.c src/bridge/script_io.c src/bridge/mission_models.c
    src/bridge/faction.c
    src/bridge/interface_platform.c src/bridge/interface_controls.c
    src/bridge/controls.c src/bridge/statistics.c src/bridge/worker_runtime.c
    src/bridge/command_builders.c src/bridge/ui_models.c src/bridge/drawing_lines.c
    src/bridge/entity_query_models.c
    src/bridge/entity_state.c src/bridge/entity_identity.c src/bridge/entity_init.c src/bridge/entity_commands.c
    src/bridge/entity_paths.c src/bridge/entity_motion.c src/bridge/entity_arrival.c src/bridge/entity_adjust.c
    src/bridge/entity_resources.c src/bridge/entity_activity.c src/bridge/entity_orders.c src/bridge/entity_contact.c
    src/bridge/entity_tick.c src/bridge/entity_decision.c src/bridge/entity_ai.c src/bridge/entity_search.c
    src/bridge/entity_main.c src/bridge/entity_combat.c src/bridge/faction_ai.c src/bridge/faction_execution.c
    src/bridge/world_controls.c src/bridge/road_topology.c
    src/bridge/entity_models.c src/bridge/script_runtime.c
    src/bridge/legacy_models.c src/bridge/script_text.c src/bridge/script_input.c
    src/bridge/midi.c src/bridge/wav_sample.c src/bridge/instruments.c
    src/bridge/commands.c src/bridge/commands_models.c
    src/bridge/messages.c src/bridge/notifications.c src/bridge/message_models.c
    src/bridge/selection.c src/bridge/selection_panels.c src/bridge/panel_text.c src/bridge/selection_models.c
    src/bridge/movie_runtime.c src/bridge/movie_present.c src/bridge/movie_color.c
    src/bridge/movie_dispatch.c src/bridge/movie_pixels.c src/bridge/movie_codec.c
    src/bridge/movie_iqt.c src/bridge/capture_video.c src/bridge/movie_models.c
    src/bridge/frame.c src/bridge/entity.c src/bridge/render_queue.c
    src/bridge/tile_draw.c src/bridge/entity_render.c src/bridge/entity_effects.c
    src/bridge/sprite.c src/bridge/sprite_draw.c src/bridge/sprite_blend.c
    src/bridge/font.c src/bridge/sprite_models.c
    src/bridge/display.c src/bridge/display_init.c src/bridge/palette.c
    src/bridge/display_models.c src/bridge/display_geometry.c src/bridge/render_models.c
    src/bridge/model_runtime.c src/bridge/callback_runtime.c src/bridge/terrain.c src/bridge/terrain_models.c
    src/bridge/terrain_dirty.c src/bridge/entity_visibility.c
    src/bridge/entity_queries.c src/bridge/qmixer_models.c src/bridge/qmixer_model_tables.c
    src/bridge/qmixer_control.c src/bridge/qmixer_playback.c
    src/bridge/qmixer_pcm.c src/bridge/qmixer_integer.c src/bridge/qmixer_render.c
    src/bridge/qmixer_objects.c src/bridge/qmixer_runtime.c src/bridge/qmixer_heap.c src/bridge/qmixer_crt.c src/bridge/qmixer_numeric.c
    src/bridge/qmixer_dsp.c src/bridge/qmixer_filters.c src/bridge/qmixer_delay.c
    src/bridge/qmixer_tap_delay.c
    src/bridge/qmixer_source.c src/bridge/qmixer_loading.c src/bridge/qmixer_errors.c
    src/bridge/qmixer_exports.c src/bridge/qmixer_api_catches.c src/bridge/qmixer_api_models.c
    src/bridge/qmixer_session.c src/bridge/qmixer_drivers.c src/bridge/qmixer_device.c src/bridge/qmixer_device_catches.c
    src/bridge/qmixer_startup.c src/bridge/qmixer_math.c src/bridge/qmixer_runtime_tail.c
    src/bridge/qmixer_lifetime.c src/native/mixer_data.c)
  set_source_files_properties(src/native/file.c src/native/memory.c src/native/settings.c src/native/cd.c src/native/sound.c src/native/sync.c src/native/timer.c src/native/audio.c src/native/window.c src/native/module_services.c src/native/system.c src/native/process.c src/native/environment.c src/native/locale.c src/native/resources.c PROPERTIES COMPILE_OPTIONS "-Wno-unused-parameter")
  target_include_directories(bumpkins_core PUBLIC src/bridge src/native)
  target_compile_definitions(bumpkins_core PUBLIC BB_PORTABLE_RUNTIME BB_NATIVE_RUNTIME BB_MODEL_RUNTIME)
  target_compile_options(bumpkins_core PRIVATE -fno-strict-aliasing -ffp-contract=off)
  target_link_libraries(bumpkins_core PUBLIC Threads::Threads PkgConfig::SDL2 m)
  if(BUMPKINS_PORTABLE_GAME)
    add_executable(bumpkins src/native/main.c)
    target_link_libraries(bumpkins PRIVATE bumpkins_core)
  endif()
  return()
endif()

if(WIN32)
  if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "The original executable requires a 32-bit bridge")
  endif()
  add_executable(bumpkins-bootstrap src/bootstrap.c)
  target_link_libraries(bumpkins-bootstrap PRIVATE advapi32)
  target_link_options(bumpkins-bootstrap PRIVATE -static -static-libgcc)

  add_library(bumpkins_bridge SHARED src/bridge/hooks.c src/bridge/call_i386.c src/bridge/loading_progress.c
    src/bridge/cd_control.c src/bridge/save_codec.c src/bridge/archive.c
    src/bridge/archive_write.c src/bridge/entity.c src/bridge/sprite.c
    src/bridge/sprite_draw.c src/bridge/sprite_blend.c src/bridge/font.c
    src/bridge/palette.c src/bridge/display.c src/bridge/display_geometry.c src/bridge/display_init.c
    src/bridge/render_queue.c src/bridge/frame.c src/bridge/tile_draw.c src/bridge/terrain_dirty.c
    src/bridge/entity_effects.c src/bridge/entity_render.c
    src/bridge/entity_queries.c src/bridge/entity_lifecycle.c
    src/bridge/entity_state.c src/bridge/entity_init.c
    src/bridge/entity_identity.c src/bridge/entity_commands.c
    src/bridge/entity_paths.c src/bridge/entity_motion.c src/bridge/entity_arrival.c
    src/bridge/entity_adjust.c src/bridge/entity_resources.c src/bridge/faction.c
    src/bridge/faction_ai.c src/bridge/faction_execution.c src/bridge/entity_activity.c
    src/bridge/placement.c src/bridge/entity_orders.c src/bridge/commands.c src/bridge/entity_contact.c src/bridge/entity_visibility.c src/bridge/entity_tick.c src/bridge/entity_decision.c src/bridge/entity_ai.c src/bridge/entity_search.c src/bridge/entity_main.c src/bridge/entity_combat.c src/bridge/random.c src/bridge/terrain.c src/bridge/notifications.c src/bridge/messages.c src/bridge/panel_text.c src/bridge/script_records.c src/bridge/script_input.c src/bridge/script_runtime.c src/bridge/script_text.c src/bridge/script_io.c src/bridge/letters.c src/bridge/mission_io.c src/bridge/world.c src/bridge/coordinator.c src/bridge/camera.c src/bridge/input.c src/bridge/input_support.c src/bridge/startup.c src/bridge/dialog.c src/bridge/statistics.c src/bridge/command_builders.c src/bridge/selection.c src/bridge/selection_panels.c src/bridge/controls.c src/bridge/road_topology.c src/bridge/interface_controls.c src/bridge/interface_platform.c src/bridge/interface_audio.c src/bridge/worker_runtime.c src/bridge/indexed_text.c src/bridge/session_controls.c src/bridge/bootstrap_resources.c src/bridge/world_controls.c src/bridge/drawing_lines.c src/bridge/global_lifecycle.c src/bridge/static_initializers.c src/bridge/object_initializers.c src/bridge/allocation_unwind.c src/bridge/capture_video.c src/bridge/movie_runtime.c src/bridge/movie_present.c src/bridge/movie_color.c src/bridge/movie_dispatch.c src/bridge/movie_pixels.c src/bridge/movie_codec.c src/bridge/movie_iqt.c src/bridge/midi.c src/bridge/wav_sample.c src/bridge/instruments.c src/bridge/media_devices.c src/bridge/crt_output.c src/bridge/crt_input.c src/bridge/crt_stream.c src/bridge/crt_descriptor.c src/bridge/crt_heap.c src/bridge/crt_debug_heap.c src/bridge/crt_primitives.c src/bridge/crt_numeric.c src/bridge/crt_format.c src/bridge/crt_bootstrap.c src/bridge/crt_numeric_startup.c src/bridge/crt_startup_data.c src/bridge/crt_stdio.c src/bridge/crt_alloc.c src/bridge/crt_locale.c src/bridge/crt_character.c src/bridge/crt_time.c src/bridge/crt_errors.c src/bridge/crt_signals.c src/bridge/crt_math_base.c src/bridge/crt_math_errors.c src/bridge/crt_math_helpers.c src/bridge/crt_calculation.c src/bridge/crt_entry.c src/bridge/crt_seh.c src/bridge/crt_cpp_objects.c src/bridge/crt_cpp_dispatch.c src/bridge/audio_routing.c src/bridge/resource_audio.c src/bridge/sample_audio.c src/bridge/sample_effects.c src/bridge/audio_cues.c src/bridge/qmixer_control.c src/bridge/qmixer_vtables.c src/bridge/qmixer_playback.c src/bridge/qmixer_pcm.c src/bridge/qmixer_render.c src/bridge/qmixer_integer.c src/bridge/qmixer_objects.c src/bridge/qmixer_dsp.c src/bridge/qmixer_filters.c src/bridge/qmixer_delay.c src/bridge/qmixer_source.c src/bridge/qmixer_loading.c src/bridge/qmixer_startup.c src/bridge/qmixer_exports.c src/bridge/qmixer_session.c src/bridge/qmixer_drivers.c src/bridge/qmixer_device.c src/bridge/qmixer_runtime.c src/bridge/qmixer_heap.c src/bridge/qmixer_crt.c src/bridge/qmixer_errors.c src/bridge/qmixer_math.c src/bridge/qmixer_numeric.c src/bridge/qmixer_math_errors.c src/bridge/qmixer_fp_errors.c src/bridge/qmixer_format.c src/bridge/qmixer_convert.c src/bridge/qmixer_decimal.c src/bridge/qmixer_parse.c src/bridge/qmixer_nls.c src/bridge/qmixer_divide.c src/bridge/qmixer_divide_abi.c src/bridge/qmixer_remainder.c src/bridge/qmixer_signals.c src/bridge/qmixer_memmove.c src/bridge/qmixer_math_adapters.c src/bridge/qmixer_numeric_dispatch.c src/bridge/qmixer_calculation.c src/bridge/qmixer_math_wrappers.c src/bridge/qmixer_tap_delay.c src/bridge/qmixer_seh.c src/bridge/qmixer_cpp_objects.c src/bridge/qmixer_terminal.c src/bridge/qmixer_cpp_dispatch.c src/bridge/qmixer_runtime_tail.c src/bridge/qmixer_cpp_handlers.c src/bridge/qmixer_api_catches.c src/bridge/qmixer_device_catches.c)
  set_target_properties(bumpkins_bridge PROPERTIES PREFIX "")
  target_compile_options(bumpkins_bridge PRIVATE -fno-strict-aliasing -ffp-contract=off
    -mstackrealign)
  target_link_options(bumpkins_bridge PRIVATE -static -static-libgcc)
elseif(BUMPKINS_NATIVE)
  if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "The i386 target requires nix develop .#native (32-bit); use -DBUMPKINS_PORTABLE_GAME=ON for the 64-bit game")
  endif()
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
  add_executable(bumpkins src/native/main.c src/native/fatal.c src/native/pe.c src/native/kernel.c
    src/native/module.c src/native/modules.c src/native/module_services.c src/native/system.c src/native/process.c src/native/environment.c src/native/locale.c src/native/resources.c src/native/module_names.c
    src/native/memory.c src/native/file.c src/native/settings.c src/native/profile.c src/native/clock.c src/native/sync.c src/native/timer.c src/native/kernel_handle.c
    src/native/timezone.c
    src/native/codepage.c
    src/native/unicode.c
    src/native/path.c
    src/native/mixer.c src/native/mixer_data.c src/native/mixer_api.c
    src/native/game.c src/native/game_data.c src/native/game_assets.c
    src/native/entries.c
    src/native/window.c src/native/draw.c src/native/audio.c src/native/sound.c
    src/native/cd.c src/native/exception.c src/bridge/call_i386.c src/bridge/loading_progress.c src/bridge/cd_control.c
    src/bridge/save_codec.c src/bridge/archive.c src/bridge/archive_write.c
    src/bridge/entity.c src/bridge/sprite.c src/bridge/sprite_draw.c
    src/bridge/sprite_blend.c src/bridge/font.c src/bridge/palette.c
    src/bridge/display.c src/bridge/display_geometry.c src/bridge/display_init.c src/bridge/render_queue.c
    src/bridge/frame.c src/bridge/tile_draw.c src/bridge/terrain_dirty.c src/bridge/entity_effects.c
    src/bridge/entity_render.c src/bridge/entity_queries.c
    src/bridge/entity_lifecycle.c src/bridge/entity_state.c
    src/bridge/entity_init.c src/bridge/entity_identity.c
    src/bridge/entity_commands.c src/bridge/entity_paths.c src/bridge/entity_motion.c
    src/bridge/entity_arrival.c src/bridge/entity_adjust.c src/bridge/entity_resources.c src/bridge/faction.c
    src/bridge/faction_ai.c src/bridge/faction_execution.c src/bridge/entity_activity.c
    src/bridge/placement.c src/bridge/entity_orders.c src/bridge/commands.c src/bridge/entity_contact.c src/bridge/entity_visibility.c src/bridge/entity_tick.c src/bridge/entity_decision.c src/bridge/entity_ai.c src/bridge/entity_search.c src/bridge/entity_main.c src/bridge/entity_combat.c src/bridge/random.c src/bridge/terrain.c src/bridge/notifications.c src/bridge/messages.c src/bridge/panel_text.c src/bridge/script_records.c src/bridge/script_input.c src/bridge/script_runtime.c src/bridge/script_text.c src/bridge/script_io.c src/bridge/letters.c src/bridge/mission_io.c src/bridge/world.c src/bridge/coordinator.c src/bridge/camera.c src/bridge/input.c src/bridge/input_support.c src/bridge/startup.c src/bridge/dialog.c src/bridge/statistics.c src/bridge/command_builders.c src/bridge/selection.c src/bridge/selection_panels.c src/bridge/controls.c src/bridge/road_topology.c src/bridge/interface_controls.c src/bridge/interface_platform.c src/bridge/interface_audio.c src/bridge/worker_runtime.c src/bridge/indexed_text.c src/bridge/session_controls.c src/bridge/bootstrap_resources.c src/bridge/world_controls.c src/bridge/drawing_lines.c src/bridge/global_lifecycle.c src/bridge/static_initializers.c src/bridge/object_initializers.c src/bridge/allocation_unwind.c src/bridge/capture_video.c src/bridge/movie_runtime.c src/bridge/movie_present.c src/bridge/movie_color.c src/bridge/movie_dispatch.c src/bridge/movie_pixels.c src/bridge/movie_codec.c src/bridge/movie_iqt.c src/bridge/midi.c src/bridge/wav_sample.c src/bridge/instruments.c src/bridge/media_devices.c src/bridge/crt_output.c src/bridge/crt_input.c src/bridge/crt_stream.c src/bridge/crt_descriptor.c src/bridge/crt_heap.c src/bridge/crt_debug_heap.c src/bridge/crt_primitives.c src/bridge/crt_numeric.c src/bridge/crt_format.c src/bridge/crt_bootstrap.c src/bridge/crt_numeric_startup.c src/bridge/crt_startup_data.c src/bridge/crt_stdio.c src/bridge/crt_alloc.c src/bridge/crt_locale.c src/bridge/crt_character.c src/bridge/crt_time.c src/bridge/crt_errors.c src/bridge/crt_signals.c src/bridge/crt_math_base.c src/bridge/crt_math_errors.c src/bridge/crt_math_helpers.c src/bridge/crt_calculation.c src/bridge/crt_entry.c src/bridge/crt_seh.c src/bridge/crt_cpp_objects.c src/bridge/crt_cpp_dispatch.c src/bridge/audio_routing.c src/bridge/resource_audio.c src/bridge/sample_audio.c src/bridge/sample_effects.c src/bridge/audio_cues.c src/bridge/qmixer_control.c src/bridge/qmixer_vtables.c src/bridge/qmixer_playback.c src/bridge/qmixer_pcm.c src/bridge/qmixer_render.c src/bridge/qmixer_integer.c src/bridge/qmixer_objects.c src/bridge/qmixer_dsp.c src/bridge/qmixer_filters.c src/bridge/qmixer_delay.c src/bridge/qmixer_source.c src/bridge/qmixer_loading.c src/bridge/qmixer_startup.c src/bridge/qmixer_exports.c src/bridge/qmixer_session.c src/bridge/qmixer_drivers.c src/bridge/qmixer_device.c src/bridge/qmixer_runtime.c src/bridge/qmixer_heap.c src/bridge/qmixer_crt.c src/bridge/qmixer_errors.c src/bridge/qmixer_math.c src/bridge/qmixer_numeric.c src/bridge/qmixer_math_errors.c src/bridge/qmixer_fp_errors.c src/bridge/qmixer_format.c src/bridge/qmixer_convert.c src/bridge/qmixer_decimal.c src/bridge/qmixer_parse.c src/bridge/qmixer_nls.c src/bridge/qmixer_divide.c src/bridge/qmixer_divide_abi.c src/bridge/qmixer_remainder.c src/bridge/qmixer_signals.c src/bridge/qmixer_memmove.c src/bridge/qmixer_math_adapters.c src/bridge/qmixer_numeric_dispatch.c src/bridge/qmixer_calculation.c src/bridge/qmixer_math_wrappers.c src/bridge/qmixer_tap_delay.c src/bridge/qmixer_seh.c src/bridge/qmixer_cpp_objects.c src/bridge/qmixer_terminal.c src/bridge/qmixer_cpp_dispatch.c src/bridge/qmixer_runtime_tail.c src/bridge/qmixer_cpp_handlers.c src/bridge/qmixer_api_catches.c src/bridge/qmixer_device_catches.c)
  target_link_libraries(bumpkins PRIVATE PkgConfig::SDL2 pthread m)
  # Native libc structures must not inherit the old guest CRT's time/file ABI.
  target_compile_definitions(bumpkins PRIVATE BB_NATIVE_RUNTIME
    _FILE_OFFSET_BITS=64 _TIME_BITS=64)
  target_compile_options(bumpkins PRIVATE -fno-strict-aliasing -ffp-contract=off -mstackrealign
    -Wno-unused-parameter)
else()
  add_executable(bumpkins src/launcher.c)
endif()
