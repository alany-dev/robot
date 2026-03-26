# Repository Guidelines

## Project Structure & Module Organization
`src/` is the main ROS1 catkin workspace. Core packages are grouped by function: control and navigation (`base_control`, `robot_navigation`), perception (`usb_camera`, `img_decode`, `img_encode`, `rknn_yolov6`, `object_track`), messaging and transport (`ai_msgs`, `monitor`, `shm_transport`), plus launch and description packages such as `pkg_launch`, `test11_description`, and `urdf_tutorial`. `ros2_ws/src/` holds ROS2 ports and experiments, currently including `usb_camera`, `img_decode`, and `cpp_pubsub`. Python agent code lives in `src/agent/`; web UI is `web_controller.py` with HTML templates in `templates/`.

Treat `llama.cpp/`, `offline-agent/`, `rknn_export/`, and `src/ydlidar/YDLidar-SDK/` as vendor-heavy trees. Keep edits there narrowly scoped and avoid drive-by formatting changes.

## Build, Test, and Development Commands
Use `catkin_make` from the repository root to build ROS1 packages, then `source devel/setup.bash` before running nodes. Launch the integrated ROS1 stack with `roslaunch pkg_launch all.launch`, or run package-local launch files from `src/*/launch/` for focused checks.

Use `cd ros2_ws && colcon build --symlink-install` for ROS2 work. Run `cd ros2_ws && colcon test && colcon test-result --verbose` before opening a ROS2 PR. For Fast DDS shared-memory setups, export the variables documented in `ros2_ws/env.md` before running camera or image pipelines.

## Coding Style & Naming Conventions
Follow the root `.clang-format`: Google-based C++ style, C++11, 2-space indentation, and grouped includes. Run `clang-format -i path/to/file.cpp` on changed C++ files. Keep package, node, launch, and config names in `snake_case`, matching existing examples such as `img_decode.launch`, `test_v4l2.cpp`, and `laser_test.cpp`. Place headers in `include/`, implementation in `src/`, launch files in `launch/`, and configs in `config/`.

## Testing Guidelines
ROS2 packages already wire in `ament_lint_auto`; ROS1 test coverage is lighter and mostly package-local. Add new tests beside the package you change, for example `src/<pkg>/test/` or `ros2_ws/src/<pkg>/src/test_*.cpp`. For hardware-dependent features, include the exact launch command and key expected log lines in the PR if full automation is not practical.

## Commit & Pull Request Guidelines
Match the existing commit style: short imperative subjects with a subsystem prefix in brackets, such as `[ROS2] add ROS2 QoS` or `[Monitor] init ros-monitor node`. Keep each commit scoped to one subsystem. PRs should state which workspace and packages changed, list the commands you ran, note hardware or model dependencies, and attach logs or screenshots for UI, camera, or navigation changes.

## Security & Configuration Tips
Do not commit local secrets or runtime state from `src/agent/config.json` or `src/agent/memory.json`; use `src/agent/config.json.example` as the template. Keep generated artifacts under `build/`, `devel/`, `install/`, and log files out of commits unless a change explicitly vendors them.
