#include "radxa_drivers/radxa_services.hpp"
#include <chrono>
#include <csignal>
#include <iostream>
#include <sys/types.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace oro {

int RadxaServices::playback_pid = -1;
int RadxaServices::current_track = 0;
bool RadxaServices::is_paused = false;

std::string RadxaServices::process_command(const nlohmann::json &cmd) {
  std::string topic = cmd.value("topic", "");
  float val = cmd.value("value", 0.0f);

  // Mandate a brief "processor delay" to simulate system work
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  if (topic == "/commands/feed") {
    return handle_feed(val);
  } else if (topic == "/commands/treat/dispense") {
    return handle_treat_dispense(val);
  } else if (topic == "/commands/photo_capture") {
    return handle_photo_capture();
  } else if (topic == "/commands/live_session/start") {
    return handle_live_session_start();
  } else if (topic == "/commands/live_session/end") {
    return handle_live_session_end();
  } else if (topic == "/commands/camera/ir_control") {
    return handle_ir_control(val >= 0.5f);
  } else if (topic == "/commands/audio/speakers") {
    return handle_audio_command(static_cast<int>(val));
  } else if (topic == "/commands/settings/apply") {
    return handle_apply_settings(cmd);
  } else if (topic == "/commands/firmware/update") {
    return handle_firmware_update();
  }

  return "{\"status\":\"error\",\"message\":\"unsupported_radxa_topic\"}";
}

std::string RadxaServices::handle_feed(float grams) {
  std::cout << "[RadxaServices] Executing Feed: " << grams << "g" << std::endl;
  nlohmann::json res = {
      {"status", "success"}, {"operation", "feed"}, {"grams", grams}};
  res["completed_at"] = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  return res.dump();
}

std::string RadxaServices::handle_treat_dispense(float count) {
  std::cout << "[RadxaServices] Executing Treat Dispense: " << count << " units" << std::endl;
  nlohmann::json res = {
      {"status", "success"}, {"operation", "treat_dispense"}, {"count", count}};
  res["completed_at"] = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  return res.dump();
}

std::string RadxaServices::handle_photo_capture() {
  std::cout << "[RadxaServices] Executing Photo Capture on /dev/video1" << std::endl;

  auto now = std::chrono::system_clock::now();
  auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                       now.time_since_epoch())
                       .count();

  std::string filename = "ORoBase_IMG_" + std::to_string(timestamp) + ".jpg";
  std::string full_path = "/home/ogmen/Pictures/Comman_Executor_Images/" + filename;

  // Use ffmpeg to capture one frame from /dev/video1
  // -y overwrites, -f v4l2 is the format, -i /dev/video1 input, -frames:v 1 capture 1 frame
  std::string cmd = "ffmpeg -y -f v4l2 -i /dev/video1 -frames:v 1 " + full_path + " > /dev/null 2>&1";

  int ret = std::system(cmd.c_str());
  bool success = (ret == 0);

  nlohmann::json res = {{"status", success ? "success" : "failed"},
                        {"operation", "photo_capture"},
                        {"saved", success},
                        {"file_id", "IMG_" + std::to_string(timestamp)},
                        {"storage_path", full_path}};
  res["completed_at"] = std::chrono::system_clock::to_time_t(now);

  if (!success) {
    res["error"] = "ffmpeg_failed_or_device_busy";
    std::cerr << "[RadxaServices] Photo capture failed for " << full_path << std::endl;
  } else {
    std::cout << "[RadxaServices] Photo saved to " << full_path << std::endl;
  }

  return res.dump();
}

std::string RadxaServices::handle_live_session_start() {
  std::cout << "[RadxaServices] Starting Live Session" << std::endl;
  nlohmann::json res = {{"status", "success"}, {"operation", "live_session_start"}};
  res["completed_at"] = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  return res.dump();
}

std::string RadxaServices::handle_live_session_end() {
  std::cout << "[RadxaServices] Ending Live Session" << std::endl;
  nlohmann::json res = {{"status", "success"}, {"operation", "live_session_end"}};
  res["completed_at"] = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  return res.dump();
}

std::string RadxaServices::handle_ir_control(bool enable) {
  std::cout << "[RadxaServices] IR Control: " << (enable ? "ON" : "OFF") << std::endl;
  nlohmann::json res = {{"status", "success"}, {"operation", "ir_control"}, {"enabled", enable}};
  res["completed_at"] = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  return res.dump();
}

std::string RadxaServices::handle_apply_settings(const nlohmann::json &cmd) {
  std::cout << "[RadxaServices] Applying System Settings" << std::endl;
  
  // Extract profile_id if available, else use a default
  std::string profile_id = cmd.value("settings_profile_id", "default_profile_v1");
  auto now = std::chrono::system_clock::now();
  auto applied_at = std::chrono::duration_cast<std::chrono::milliseconds>(
                        now.time_since_epoch())
                        .count();

  nlohmann::json res = {
      {"status", "success"}, 
      {"operation", "apply_settings"},
      {"settings_profile_id", profile_id},
      {"applied_at", applied_at},
      {"failure_reason", ""}
  };
  res["completed_at"] = std::chrono::system_clock::to_time_t(now);
  return res.dump();
}

std::string RadxaServices::handle_firmware_update() {
  std::cout << "[RadxaServices] Checking for Firmware Update" << std::endl;
  nlohmann::json res = {{"status", "success"},
                        {"operation", "firmware_update"},
                        {"message", "up_to_date"},
                        {"version", "v0.0.1-latest"}};
  res["completed_at"] = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  return res.dump();
}

std::string RadxaServices::handle_audio_command(int action_code) {
  std::cout << "[RadxaServices] Audio Command: Code=" << action_code
            << std::endl;

  // TODO: Adding robust and better way to play the audio files preferably with
  // a dedicated dir and inclusion of TMS 1-3: Select and Play Track X
  if (action_code >= 1 && action_code <= 3) {
    kill_playback();
    current_track = action_code;
    is_paused = false;

    std::string track_path;
    if (action_code == 1)
      track_path = "/home/radxa/Music/breaking_bad_intro.mp3";
    else if (action_code == 2)
      track_path = "/home/radxa/Music/dandelions_violin.mp3";
    else if (action_code == 3)
      track_path = "/home/radxa/Music/I_think_they_call_this_love.mp3";
    else
      track_path = "/home/radxa/Music/I_think_they_call_this_love.mp3";

    pid_t pid = fork();
    if (pid == 0) {
      // Child: Launch ffplay
      execlp("ffplay", "ffplay", "-nodisp", "-autoexit", track_path.c_str(),
             NULL);
      _exit(1); // Should not reach here
    } else if (pid > 0) {
      playback_pid = pid;
      return "{\"status\":\"success\",\"message\":\"playing_track_" +
             std::to_string(action_code) + "\"}";
    }
    return "{\"status\":\"error\",\"message\":\"failed_to_fork\"}";
  }

  // 0: Stop
  if (action_code == 0) {
    kill_playback();
    return "{\"status\":\"success\",\"message\":\"stopped\"}";
  }

  // 99: Play (last/current)
  if (action_code == 99) {
    if (is_paused && playback_pid > 0) {
      kill(playback_pid, SIGCONT);
      is_paused = false;
      return "{\"status\":\"success\",\"message\":\"playing\"}";
    }
    // If not paused, reload current track
    return handle_audio_command(current_track > 0 ? current_track : 1);
  }

  // 97: Pause
  if (action_code == 97) {
    if (playback_pid > 0 && !is_paused) {
      kill(playback_pid, SIGSTOP);
      is_paused = true;
      return "{\"status\":\"success\",\"message\":\"paused\"}";
    }
    return "{\"status\":\"error\",\"message\":\"nothing_to_pause\"}";
  }

  return "{\"status\":\"error\",\"message\":\"invalid_audio_action\"}";
}

void RadxaServices::kill_playback() {
  if (playback_pid > 0) {
    kill(playback_pid, SIGTERM);
    waitpid(playback_pid, NULL, WNOHANG);
    playback_pid = -1;
    is_paused = false;
  }
}

} // namespace oro
