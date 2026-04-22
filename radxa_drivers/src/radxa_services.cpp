#include "radxa_services.hpp"
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
    return simulate_work("Feed (Grams)", val);
  } else if (topic == "/commands/treat/dispense") {
    return simulate_work("Treat Dispense (Count)", val);
  } else if (topic == "/commands/photo_capture") {
    return simulate_work("Photo Capture", val);
  } else if (topic == "/commands/live_session/start") {
    return simulate_work("Live Session Start", val);
  } else if (topic == "/commands/live_session/end") {
    return simulate_work("Live Session End", val);
  } else if (topic == "/commands/camera/ir_control") {
    return simulate_work("Camera IR Control", val);
  } else if (topic == "/commands/audio/speakers") {
    // TODO: Add robust method to play the audio files
    return handle_audio_command(static_cast<int>(val));
  } else if (topic == "/commands/settings/apply") {
    return simulate_work("Apply System Settings", val);
  } else if (topic == "/commands/firmware/update") {
    // TODO: Add robust method to fetch and install the firmware
    nlohmann::json response;
    response["status"] = "success";
    response["message"] = "up_to_date";
    response["version"] = "v0.0.1-latest";
    response["source"] = "RadxaCubieA7z";
    response["completed_at"] =
        std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    return response.dump();
  }

  return "{\"status\":\"error\",\"message\":\"unsupported_radxa_topic\"}";
}

std::string RadxaServices::simulate_work(const std::string &name, float value) {
  std::cout << "[RadxaServices] JSON Command Executed: " << name
            << " (Val: " << value << ")" << std::endl;

  nlohmann::json response;
  response["status"] = "success";
  response["source"] = "RadxaCubieA7z";
  response["operation"] = name;
  response["requested_value"] = value;
  response["completed_at"] =
      std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());

  return response.dump();
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
      track_path = "/home/ogmen/Music/breaking_bad_intro.mp3";
    else if (action_code == 2)
      track_path = "/home/ogmen/Music/dandelions_violin.mp3";
    else if (action_code == 3)
      track_path = "/home/ogmen/Music/I_think_they_call_this_love.mp3";
    else
      track_path = "/home/ogmen/Music/I_think_they_call_this_love.mp3";

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
